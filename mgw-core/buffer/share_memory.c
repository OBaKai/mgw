#include "stream_buff.h"
#include <sys/shm.h>
#include <assert.h>
#include <sys/types.h> //Specified in man 2 open
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h> //Allows use of error numbers
#include <fcntl.h> //Specified in man 2 open
#include <stdio.h>
#include <unistd.h>

char *CreateShareMemory(int *Fd, const char *name, int size, int frames, void *priv_data)
{
	unsigned int uiShareHeadSize = sizeof(SmemoryHead);
	unsigned int uiShareFrameSize = sizeof(SmemoryFrame) * frames;
	int fd, shmid;
	char filename[128];
	snprintf(filename, 128, "/tmp/.%s", name);
	umask(0);
	if ((fd = open(filename, O_RDWR | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG | S_IRWXO)) > 0)
	{
		close(fd);
	}

	unsigned int uiShareSize = size + uiShareHeadSize + uiShareFrameSize;
	key_t key = ftok(filename, 'k');
	int iFd = shmid = shmget(key, uiShareSize, IPC_CREAT | S_IRWXU | S_IRWXG | S_IRWXO);
	if( iFd == -1 )
	{
		printf("key=%d size=%d/%d\n", key, size, uiShareSize);
		perror("shmget alloc share mem error:");
		return NULL;
	}
//	assert( iFd != -1 );
	*Fd = iFd;

	char *memory = (char *) shmat(shmid, NULL, 0);
	if( (long)memory == -1 )
	{
		perror("shmat alloc share mem error:");
		return NULL;
	}
	//assert( (long)memory != -1 );
	struct shmid_ds buf;
	shmctl(shmid, IPC_STAT, &buf);
	if (buf.shm_nattch == 1)
	{
		InitMemoryParam(memory, frames, size, priv_data);
	}
	//SmemoryHead *pstuHead = (SmemoryHead *)memory;

	return memory;
}

void DeleteShareMemory(void *head)
{
	BuffContext *pbuf = (BuffContext *)head;
	if (pbuf->iFd < 0)
	{
		return;
	}

	shmdt( (void*)pbuf->position.pstuHead );

	struct shmid_ds buf;
	shmctl(pbuf->iFd, IPC_STAT, &buf);
	printf("buf.shm_nattch=%d", buf.shm_nattch);
	if (buf.shm_nattch == 0)
	{
		shmctl(pbuf->iFd, IPC_RMID, NULL);
		pbuf->iFd = -1;

		char filename[128];
		snprintf(filename, 128, "/tmp/.%s", pbuf->Name);
		remove(filename);
		printf("(%s %s) ***delete share memory fd=%d name=%s\n", pbuf->Name, pbuf->UserId, pbuf->iFd, pbuf->Name);
	}
}

