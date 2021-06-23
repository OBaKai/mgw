#include "stream_buff.h"
#include "list_function.h"
#include <stdlib.h>
#include "malloc_memory.h"

SC_node *g_memory_head = NULL;
int g_memory_nu = 0;

char *CreateMallocMemory(int *Fd, const char *name, int size, int frames, io_mode_t mode, void *priv_data)
{
	if(g_memory_head)
	{
		char *pbuff = NULL;
		SC_node *p = list_de(g_memory_head, &pbuff, (char *)name);
		if(p)
		{
			SmemoryHead *h = (SmemoryHead *)pbuff;
			if(IO_MODE_WRITE == mode && h->ucWriterCount > 0)
			{
				fprintf(stderr, "the buff has a writer");
				return NULL;
			}
			return pbuff;
		}
	}
	char *pbuf = NULL;

	unsigned int uiHeadSize = sizeof(SmemoryHead);
	unsigned int uiFrameSize = sizeof(SmemoryFrame) * frames;
	unsigned int uiSize = size + uiHeadSize + uiFrameSize;
	pbuf = (char *)calloc(1, uiSize);
	if(!pbuf)
	{
		return NULL;
	}

	InitMemoryParam(pbuf, frames, size, priv_data);
	*Fd = 0;
	if(!g_memory_head)
	{
		g_memory_head = list_init();
		g_memory_nu = 1;
	}

	if(!list_en(g_memory_head, pbuf, (char*)name))
	{
		free(pbuf);
		return NULL;
	}
	g_memory_nu++;
	//SmemoryHead *pstuHead = (SmemoryHead *)pbuf;
	return pbuf;
}

int DeleteMallocMemory(const char *name)
{
	if(g_memory_head)
	{
		char *pbuff = NULL;
		SC_node *p = list_de(g_memory_head, &pbuff, (char*)name);
		if(p)
		{
			free(pbuff);
			list_free(g_memory_head, (char*)name);
		}
		g_memory_nu--;
		if(1 == g_memory_nu)
		{
			g_memory_nu = 0;
			free(g_memory_head);
			g_memory_head = NULL;
		}
	}

	return 0;
}


