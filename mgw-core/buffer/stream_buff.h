#ifndef __STREAM_BUFF_H__
#define __STREAM_BUFF_H__
#include <stdio.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include "util/codec-def.h"

#ifdef __cplusplus
extern "C"{
#endif

#define MAX_MEMORY_BANK	1

typedef enum mem_type {
	MEM_SHARED = 0,
	MEM_DYNAMIC,
}mem_t;

typedef enum io_mode {
	IO_MODE_READ 	= (1 << 0),
	IO_MODE_WRITE	= (1 << 1),
}io_mode_t;

typedef struct _SMemFrameInfo
{
	unsigned long long timestamp;
	frame_t frametype;//0:I frame
    int priority;
	char reserved[3];
}SMemFrameInfo;

typedef struct _SmemoryFrame
{
	/* Flag of valid frame */
	char ucValidFlag;
	char reserved[3];
	 /*the offet of start address*/
	unsigned int position;
	/*the frame length */
	unsigned int len;
	/* Frame information */
	SMemFrameInfo stuFrameInfo;
}SmemoryFrame;

/** Share memory context */
typedef struct _SmemoryHead
{
	int magic;
	/** size of continuously data */
	unsigned int datasize;
	/* Count frames of write */
	unsigned int uiWritFrameCount;
	/* Count of max valid frames */
	unsigned int uiMaxValidFrames;
	/* Mutex of write */
	int n32WriterPid;
	/* Count of writer */
	unsigned char ucWriterCount;
	/* Count of reader */
	unsigned char ucReaderCount;
	/* Block Write or not */
	unsigned char bLock;
	char *pnext;
	char *prev;
	char reserved[4];

    void *priv_data;
}SmemoryHead;

typedef struct _SMemoryPosition
{
	//SmemoryHead
	char *pstuHead;
	//SmemoryFrame
	char *pstuFrames;
	//buff
	char *pstuData;
}SMemoryPosition;

typedef struct tag_MemReader
{
	/** Aready read frame count */
	unsigned int u32RdFrameCount;
	/**Read pointer in share memory*/
	unsigned int u32RdSharememPos;
	bool breIframe;
	 /** flag of reset data, check invalid data */
	bool bResetPos;
	/** Read data by time or not */
	bool bReadByTime;
}MemReader_t;

typedef struct tag_MemWriter
{
	
}MemWriter_t;

typedef struct BuffContext 
{
    void *priv_data;
	/** Memory address of start */
	SMemoryPosition position;
	int iFd;
	char UserId[32];
	char Name[64];
	io_mode_t mode;
	//SHARE_MEMORY	= 0x00, MALLOC_MEMORY = 0x01,
	int type;
	//MemWriter_t
	char *pWritepara;
	//MemReader_t
	char *pReadpara;
}BuffContext;

typedef struct _FrameAddrInfo_
{
	char *pframe;
	unsigned int len;
}FrameAddrInfo;

typedef struct _SGetFrameInfo_
{
	FrameAddrInfo faddr[2];
	unsigned int maxframelen;
	unsigned long long timestamp;
	char addrnum;
	frame_t frametype;
}SGetFrameInfo;
void InitMemoryParam(char *phead, int frames, int size, void *priv_data);

BuffContext *CreateStreamBuff(unsigned int size, const char *name,
							const char *id,int frames, int type,
							io_mode_t mode, int read_bytime, void *priv_data);

int DeleteStreamBuff(BuffContext *pbuf);

/*frametype:0:IFrame*/
int PutOneFrameToBuff(BuffContext *pcontext, char *pframe, uint32_t framelen,
						uint64_t timestamp, frame_t frametype, int priority);
/*By copy*/
int GetOneFrameFromBuff(BuffContext *pcontext, char **pframe, uint32_t maxframelen,
						uint64_t *timestamp, frame_t *frametype, int *priority);
/* No copy*/
int GetOneFrameFromBuff2(BuffContext *pcontext, SGetFrameInfo *pinfo);
unsigned long long CheckBuffDuration(BuffContext *pcontext);

#ifdef __cplusplus
}
#endif
#endif
