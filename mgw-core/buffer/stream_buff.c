#include "stream_buff.h"
#include "malloc_memory.h"
#include "share_memory.h"

#include <stdlib.h>
#include <string.h>

#define _printd(fmt, ...)	printf ("[%s][%d]"fmt"\n", (char *)strrchr(__FILE__, '\\')?(strrchr(__FILE__, '/') + 1):__FILE__, __LINE__, ##__VA_ARGS__)

struct buff_error_entry 
{
	int num;
	const char *tag;
};

static const struct buff_error_entry buff_error_entries[] = 
{
    { 0,							"Succeed"                     							},
    { -1,     						"Invalid parameter"         							},
	{ -2,							"The freame is too big" 								},
};

int buff_strerror(int errnum, char *errbuf, size_t errbuf_size)
{
    int ret = 0, i;
    const struct buff_error_entry *entry = NULL;

    for (i = 0; i < (sizeof(buff_error_entries) / sizeof(buff_error_entries[0])); i++)
	{
        if (errnum == buff_error_entries[i].num) 
		{
            entry = &buff_error_entries[i];
            break;
        }
    }
    if (entry) 
	{
        strncpy(errbuf, entry->tag, errbuf_size);
    } 
	else 
    {
        snprintf(errbuf, errbuf_size, "Error number %d occurred", errnum);
    }

    return ret;
}


void InitMemoryParam(char *phead, int frames, int size, void *priv_data)
{
	SmemoryHead *pstuHead = (SmemoryHead *)phead;
	char *pstuFrames = (char *)phead + sizeof(SmemoryHead);
	
	if( pstuHead->magic != 0x12348756 )
	{
		memset(pstuHead, 0, sizeof(SmemoryHead));
	}
	else
	{
		pstuHead->bLock = 0;
	}

	pstuHead->magic = 0x12348756;
	pstuHead->datasize = size;
	pstuHead->uiMaxValidFrames = frames;
	pstuHead->ucReaderCount = 0;
	pstuHead->ucWriterCount = 0;
	pstuHead->uiWritFrameCount = 0;
    pstuHead->priv_data = priv_data;
	memset(pstuFrames, 0, sizeof(SmemoryFrame)*frames );
	return;
}

BuffContext *CreateStreamBuff(unsigned int size, const char *name, const char *id, int frames, int type, io_mode_t mode, int read_bytime, void *priv_data)
{
	BuffContext *pbuf = NULL;
	char *pHead = NULL;
    void *source = NULL;
	if(size > 0)
	{
		pbuf = (BuffContext *)calloc(1, sizeof(BuffContext));
		if(!pbuf)
		{
			return NULL;
		}
        source = (IO_MODE_WRITE == mode) && priv_data ? priv_data : NULL;
		snprintf(pbuf->Name, sizeof(pbuf->Name), "%s", name);
		if(0 == type)
			pHead = CreateShareMemory(&pbuf->iFd, pbuf->Name, size, frames, source);
		else
			pHead = CreateMallocMemory(&pbuf->iFd, pbuf->Name, size, frames, mode, source);
		
		if(!pHead)
		{
			free(pbuf);
			return NULL;
		}
		//SmemoryHead *pstuHead = (SmemoryHead *)pHead;
		pbuf->position.pstuHead = pHead;
		pbuf->position.pstuFrames = pbuf->position.pstuHead + sizeof(SmemoryHead);
		pbuf->position.pstuData = pbuf->position.pstuFrames + (sizeof(SmemoryFrame)*frames);
		pbuf->type = type;
		
		pbuf->priv_data = priv_data;
		snprintf(pbuf->UserId, sizeof(pbuf->UserId), "%s", id);
		pbuf->mode = mode;
		SmemoryHead *h = (SmemoryHead *)pbuf->position.pstuHead;
		if(IO_MODE_READ == mode)
		{
			h->ucReaderCount++;
			MemReader_t *read = (MemReader_t *)calloc(1, sizeof(MemReader_t));
			pbuf->pReadpara = (char *)read;
			pbuf->pWritepara = NULL;
			read->breIframe = true;
			if(read_bytime)
			{
				read->bReadByTime = true;
			}
			else
			{
				read->bReadByTime = false;
			}
		}
		else
		{
			if(h->ucWriterCount > 0)
			{
				free(pbuf);
				_printd("the buff %s has a writer", name);
				return NULL;
			}
			h->ucWriterCount++;
			pbuf->pReadpara = NULL;
			pbuf->pWritepara = NULL;
		}
	}

	return pbuf;
}


int DeleteStreamBuff(BuffContext *pbuf)
{
	int ret = 0;
	SmemoryHead *h = (SmemoryHead *)pbuf->position.pstuHead;
	if(IO_MODE_WRITE == pbuf->mode)
	{	
		h->ucWriterCount--;
	}
	else
	{
		h->ucReaderCount--;
	}
	
	if(0 == h->ucReaderCount && 0 == h->ucWriterCount)
	{
		_printd("The ucReaderCount =%d ucWriterCount=%d,memory free ok, name=%s, userid=%s", h->ucReaderCount,h->ucWriterCount, pbuf->Name, pbuf->UserId);
		if(0 == pbuf->type)
		{
			DeleteShareMemory((void *)pbuf);
			ret = 0;
		}
		else
		{
			ret = DeleteMallocMemory(pbuf->Name);
		}
	}
	else
	{
		_printd("There is still %d reader, %d write, name=%s, userid=%s", h->ucReaderCount,h->ucWriterCount, pbuf->Name, pbuf->UserId);
		if(0 == pbuf->type)
		{
			/** Need to detach to aviod it fill out of process */
			DeleteShareMemory((void *)pbuf);
		}
//		ret = -1; 
	}
	if(pbuf->pReadpara)
	{
		free(pbuf->pReadpara);
	}
	if(pbuf->pWritepara)
	{
		free(pbuf->pWritepara);
	}
	free(pbuf);

	return ret;
}


char *SearchOneWriteBuff(BuffContext *pcontext)
{
	return NULL;
}

int CheckBuffDataCover(unsigned int pos_s, unsigned int pos_e, int next, SmemoryFrame *pstuFrames, unsigned int uiMaxValidFrames)
{
	int i;
	unsigned int npos_s = 0;
	for(i = 0; i < uiMaxValidFrames-2; i++)
	{
		npos_s = pstuFrames[next+i].position;
		if(pos_s < pos_e)
		{
			if(pos_s <= npos_s && npos_s <= pos_e)
			{
				pstuFrames[next+i].ucValidFlag = 0;
				continue;
			}
		}
		else
		{
			if(pos_s <= npos_s || npos_s <= pos_e)
			{
				pstuFrames[next+i].ucValidFlag = 0;
				continue;
			}
		}
		break;
	}
	return 0;
}

int PutOneFrameToBuff(BuffContext *pcontext, char *pframe, uint32_t framelen,
						uint64_t timestamp, frame_t frametype, int priority)
{
	if(!pcontext || !pframe)
	{
		_printd("Invalid parameter");
		return -1;
	}
	SmemoryHead *phead = (SmemoryHead *)pcontext->position.pstuHead;
	SmemoryFrame *pstuFrames = (SmemoryFrame *)pcontext->position.pstuFrames;
	char *pstart_addr = pcontext->position.pstuData;
	int w = phead->uiWritFrameCount % phead->uiMaxValidFrames;
	int wnext = (w+1) % phead->uiMaxValidFrames;
	unsigned int pos_s;
	unsigned int pos_e;
	if(phead->datasize/2 <= framelen)
	{
		_printd("the freame is too big, len=%d/%d", framelen, phead->datasize);
		return -2;
	}

	unsigned int position = pstuFrames[w].position;
	if(position + framelen <= phead->datasize)
	{
		pos_s = position;
		memcpy(pstart_addr + position, pframe, framelen);
		position += framelen;
		pos_e = position;
		if(position == phead->datasize)
		{
			position = 0;
		}
	}
	else
	{
		int left = phead->datasize - position;
		int len = framelen - left;
		pos_s = position;
		memcpy(pstart_addr + position, pframe, left);
		memcpy(pstart_addr, pframe + left, len);
		position = len;
		pos_e = position;
	}

	pstuFrames[w].len = framelen;
	pstuFrames[w].stuFrameInfo.frametype = frametype;
	pstuFrames[w].stuFrameInfo.timestamp = timestamp;
    pstuFrames[w].stuFrameInfo.priority  = priority;
	pstuFrames[w].ucValidFlag = 1;
	
//	SearchOneWriteBuff(BuffContext *pcontext);
	CheckBuffDataCover(pos_s, pos_e, wnext, pstuFrames, phead->uiMaxValidFrames);

//	pstuFrames[wnext].ucValidFlag = 0;
	pstuFrames[wnext].position = position;
	phead->uiWritFrameCount++;

	return 0;
}

int JumpToOldestIFrame(MemReader_t *pRead, SmemoryHead *phead, SmemoryFrame *pstuFrames)
{
	if(phead->uiWritFrameCount == 0)
	{
		pRead->u32RdFrameCount = 0;
		return 0;
	}
	int iTempPos = phead->uiWritFrameCount;

	int pos = iTempPos;
	int i;
	for(i=0; i < phead->uiMaxValidFrames; i++)
	{
		pos = (iTempPos+i) % phead->uiMaxValidFrames;
		if(pstuFrames[pos].ucValidFlag && pstuFrames[pos].stuFrameInfo.frametype == FRAME_I)
		{
			int iNewPos = (iTempPos+i) - phead->uiMaxValidFrames;
			if(iNewPos < 0)
			{
				iNewPos = 0;
			}
			if((unsigned int)iNewPos < pRead->u32RdFrameCount)
			{
				if(pRead->u32RdFrameCount < (unsigned int)iTempPos)
				{
					continue;
				}
			}
			pRead->u32RdFrameCount = iNewPos;
			_printd("JumpToOldestIFrame is ok");
			return 0;
		}
	}
	return -1;
}

int JumpTonewestIFrame(MemReader_t *pRead, SmemoryHead *phead, SmemoryFrame *pstuFrames)
{
	if(phead->uiWritFrameCount == 0)
	{
		pRead->u32RdFrameCount = 0;
		return 0;
	}
	int iTempPos = phead->uiWritFrameCount;
	int diff = phead->uiWritFrameCount - pRead->u32RdFrameCount;
	int min = diff > 0?pRead->u32RdFrameCount:phead->uiWritFrameCount-phead->uiMaxValidFrames;
	int pos = iTempPos;
	int i;
	for(i=iTempPos-1; i > min && i >= 0; i--)
	{
		pos = i % phead->uiMaxValidFrames;
		if(pstuFrames[pos].ucValidFlag && pstuFrames[pos].stuFrameInfo.frametype == 0)
		{
			int iNewPos = i;
			if((unsigned int)iNewPos >= pRead->u32RdFrameCount)
			{
				pRead->u32RdFrameCount = iNewPos;
				_printd("JumpTonewestIFrame is ok");
				return 0;
			}
			else
			{
				break;
			}
		}
	}
	return -1;
}

unsigned long long CheckBuffDuration(BuffContext *pcontext)
{
	if(!pcontext)
	{
		_printd("Invalid parameter");
		return 0;
	}
	SmemoryHead *phead = (SmemoryHead *)pcontext->position.pstuHead;
	SmemoryFrame *pstuFrames = (SmemoryFrame *)pcontext->position.pstuFrames;
	// char *pstart_addr = pcontext->position.pstuData;
	MemReader_t *pRead = (MemReader_t *)pcontext->pReadpara;
	int wcount = phead->uiWritFrameCount - 1;
	if(wcount <= pRead->u32RdFrameCount)
	{
		return 0;
	}
	int rp = pRead->u32RdFrameCount % phead->uiMaxValidFrames;
	int w = wcount % phead->uiMaxValidFrames;
	return pstuFrames[w].stuFrameInfo.timestamp - pstuFrames[rp].stuFrameInfo.timestamp;
}

static char * put_be32(char *output, uint32_t nVal )
{
    output[3] = nVal & 0xff;
    output[2] = nVal >> 8;
    output[1] = nVal >> 16;
    output[0] = nVal >> 24;
    return output+4;
}

int GetOneFrameFromBuff(BuffContext *pcontext, char **pframe,uint32_t maxframelen,
                        uint64_t *timestamp, frame_t *frametype, int *priority)
{
	if(!pcontext || !pframe)
	{
		_printd("Invalid parameter, pcontext(%p), pframe(%p)", pcontext, pframe);
		return -1;
	}
	SmemoryHead *phead = (SmemoryHead *)pcontext->position.pstuHead;
	SmemoryFrame *pstuFrames = (SmemoryFrame *)pcontext->position.pstuFrames;
	char *pstart_addr = pcontext->position.pstuData;
	MemReader_t *pRead = (MemReader_t *)pcontext->pReadpara;
	int rp = 0;//pRead->u32RdFrameCount % phead->uiMaxValidFrames;
	
	if(phead->uiWritFrameCount == pRead->u32RdFrameCount)
	{
		return 0;
	}
	
	if(phead->uiWritFrameCount < pRead->u32RdFrameCount)
	{
		_printd("(%s %s)  w=%d < r=%d so need jump count=%d\n", pcontext->Name, pcontext->UserId,
                        phead->uiWritFrameCount, pRead->u32RdFrameCount, phead->uiMaxValidFrames);
		JumpToOldestIFrame(pRead, phead, pstuFrames);
		return 0;
	}

	if((phead->uiWritFrameCount - pRead->u32RdFrameCount) > phead->uiMaxValidFrames)
	{
		if(!pRead->u32RdFrameCount)
		{
			_printd("(%s %s) need jump w=%d r=%d count=%d\n", pcontext->Name, pcontext->UserId,
                        phead->uiWritFrameCount, pRead->u32RdFrameCount, phead->uiMaxValidFrames);
		}
		if(JumpToOldestIFrame(pRead, phead, pstuFrames) == -1)
		{
			return 0;
		}
	}

	if(pRead->breIframe)
	{
		if(phead->uiWritFrameCount >= 5)
		{
			pRead->u32RdFrameCount = phead->uiWritFrameCount - 5;
		}
		if(JumpTonewestIFrame(pRead, phead, pstuFrames) < 0)
		{
			return 0;
		}
		pRead->breIframe = false;
	}
	
	rp = pRead->u32RdFrameCount % phead->uiMaxValidFrames;
	if(pstuFrames[rp].ucValidFlag == 0)
	{
		_printd("(%s %s)  pos=%d invalid frame ,jump to oldest iframe r=%d w=%d\n", pcontext->Name, pcontext->UserId, rp, pRead->u32RdFrameCount, phead->uiWritFrameCount);
		if(JumpToOldestIFrame(pRead, phead, pstuFrames) == -1)
		{
			return 0;
		}
		rp = pRead->u32RdFrameCount % phead->uiMaxValidFrames;
	}

	if(pstuFrames[rp].len > maxframelen)
	{
		_printd("maxframelen=%d len=%d", maxframelen, pstuFrames[rp].len);
		return 0;
	}
	
	if(pRead->bReadByTime)
	{
		unsigned long long gtime = *timestamp;
		unsigned long long ntime = pstuFrames[rp].stuFrameInfo.timestamp;
	
		if(0 != gtime && gtime < ntime && ntime - gtime > 2000)
		{
			*timestamp = pstuFrames[rp].stuFrameInfo.timestamp;
			return 0;
		}
		
	}

	int nalu_size = FRAME_AAC == pstuFrames[rp].stuFrameInfo.frametype ? 0 : 4;
	unsigned int position = pstuFrames[rp].position;
	if(position + pstuFrames[rp].len <= phead->datasize)
	{
        *pframe = bzalloc(pstuFrames[rp].len + nalu_size);
		if (nalu_size)
            put_be32((char*)*pframe, pstuFrames[rp].len);

		memcpy(*pframe + nalu_size, pstart_addr + position, pstuFrames[rp].len);
	}
	else
	{
		int left = phead->datasize - position;
		int len = pstuFrames[rp].len - left;
        *pframe = bzalloc(left + len + nalu_size);
		if (nalu_size)
            put_be32((char*)*pframe, pstuFrames[rp].len);

		memcpy(*pframe + nalu_size, pstart_addr + position, left);
		memcpy(*pframe + nalu_size + left, pstart_addr, len);
	}

	*timestamp = pstuFrames[rp].stuFrameInfo.timestamp;
	*frametype = pstuFrames[rp].stuFrameInfo.frametype;
    *priority  = pstuFrames[rp].stuFrameInfo.priority;

	pRead->u32RdFrameCount++;
	return pstuFrames[rp].len + nalu_size;
}

int GetOneFrameFromBuff2(BuffContext *pcontext, SGetFrameInfo *pinfo)
{
	if(!pcontext || !pinfo)
	{
		_printd("Invalid parameter");
		return -1;
	}
	SmemoryHead *phead = (SmemoryHead *)pcontext->position.pstuHead;
	SmemoryFrame *pstuFrames = (SmemoryFrame *)pcontext->position.pstuFrames;
	char *pstart_addr = pcontext->position.pstuData;
	MemReader_t *pRead = (MemReader_t *)pcontext->pReadpara;
	int rp = 0;//pRead->u32RdFrameCount % phead->uiMaxValidFrames;
	
	if(phead->uiWritFrameCount == pRead->u32RdFrameCount)
	{
		return 0;
	}
	
	if(phead->uiWritFrameCount < pRead->u32RdFrameCount)
	{
		_printd("(%s %s)  w=%d < r=%d so need jump count=%d\n", pcontext->Name, pcontext->UserId, phead->uiWritFrameCount, pRead->u32RdFrameCount, phead->uiMaxValidFrames);
		JumpToOldestIFrame(pRead, phead, pstuFrames);
		return 0;
	}

	if((phead->uiWritFrameCount - pRead->u32RdFrameCount) > phead->uiMaxValidFrames)
	{
		_printd("(%s %s) need jump w=%d r=%d count=%d\n", pcontext->Name, pcontext->UserId, phead->uiWritFrameCount, pRead->u32RdFrameCount, phead->uiMaxValidFrames);
		if(JumpToOldestIFrame(pRead, phead, pstuFrames) == -1)
		{
			return 0;
		}
	}

	if(pRead->breIframe)

	{
		pRead->u32RdFrameCount = phead->uiWritFrameCount - 5;
		if(JumpTonewestIFrame(pRead, phead, pstuFrames) < 0)
		{
			return 0;
		}
		pRead->breIframe = false;
	}
	
	rp = pRead->u32RdFrameCount % phead->uiMaxValidFrames;
	if(pstuFrames[rp].ucValidFlag == 0)
	{
		_printd("(%s %s)  pos=%d invalid frame ,jump to oldest iframe r=%d w=%d\n", pcontext->Name, pcontext->UserId, rp, pRead->u32RdFrameCount, phead->uiWritFrameCount);
		if(JumpToOldestIFrame(pRead, phead, pstuFrames) == -1)
		{
			return 0;
		}
		rp = pRead->u32RdFrameCount % phead->uiMaxValidFrames;
	}

	unsigned int position = pstuFrames[rp].position;
	if(position + pstuFrames[rp].len <= phead->datasize)
	{
		pinfo->addrnum = 1;
		pinfo->faddr[0].len = pstuFrames[rp].len;
		pinfo->faddr[0].pframe = pstart_addr + position;
	}
	else
	{
		int left = phead->datasize - position;
		int len = pstuFrames[rp].len - left;
		
		pinfo->addrnum = 2;
		pinfo->faddr[0].len = left;
		pinfo->faddr[0].pframe = pstart_addr + position;
		pinfo->faddr[1].len = len;
		pinfo->faddr[1].pframe = pstart_addr;
	}
	pinfo->timestamp = pstuFrames[rp].stuFrameInfo.timestamp;
	pinfo->frametype = pstuFrames[rp].stuFrameInfo.frametype;

	pRead->u32RdFrameCount++;
	return pstuFrames[rp].len;
}

