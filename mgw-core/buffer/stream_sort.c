#include "stream_sort.h"
#include <time.h>
#include <stdlib.h>
#include <assert.h>

#include "util/base.h"
#include "util/bmem.h"

#define DEFAULT_SORT_TIME	1000*1000//us
#define MAX_SORT_BUFF_LEN	8*1024*1024//byte
#define MIM_SORT_BUFF_LEN	1*1024*1024//byte

typedef struct _SC_ssnode_
{
	/*the data of start address*/
	char *position;
	/*the frame length */
	unsigned int len;
	unsigned long long timestamp;
	char frametype;
    int priority;
	/* Flag of valid frame */
	char ucValidFlag;
	char reserved[2];
	struct _SC_ssnode_ *prev;
	struct _SC_ssnode_ *next;
}SC_ssnode;

typedef struct _SC_frameaddr_
{
	/*the data of start address*/
	unsigned int position;
	struct _SC_frameaddr_ *prev;
	struct _SC_frameaddr_ *next;
}SC_frameaddr;

typedef struct _SC_ssbuff_
{
	int magic; /**/	
	/* prival */
	void *puser;
	/* Time sort list */
	SC_ssnode *phead;
	/* Frame list by address */
	SC_frameaddr *frameaddr;
	unsigned int datasize;
	/*the data of start address*/
	char *pdata;
	/*offset address**/
	unsigned int position;
	/* Count frames of input */
	unsigned int uiPutFrameCount;
	unsigned int uiValidLen;
	unsigned int uiSortTime;
	/* sort time bakup and resume */
	unsigned int uiSortTimeBak;
	unsigned int uiMaxSortTime;
	/* Count time of sort */
	unsigned int uiAnalyTime;
	unsigned long long maxtimestamp;
	unsigned long long mintimestamp;
	unsigned long long premintimestamp;
	
	/* Count frames of output */
	unsigned int u32PopFrameCount;
	/* Droped count of too late frame */
	unsigned int u32LateFrameCount;
	/* Droped count of too early */
	unsigned int u32EarlyFrameCount;
	/* Continuously droped count of too early */
	unsigned int u32EarlyFrameDrop;
	/* Continuously droped count of too late */
	unsigned int u32LateFrameDrop;
	
	/* Memory free size */
	unsigned int freesize;
	/* Memory free time */
	unsigned long long freetime;
	/* Count resort time */
	unsigned long long analytime;
	
	struct dstr name;
	struct dstr userid;
	
	SortDataCallback Datacallback;
}SC_ssbuff;


int DefaultStreamSortBuff(SC_ssbuff *pssbuf, unsigned int uiSize, unsigned int uiSortTime, unsigned int uiMaxSortTime)
{
	pssbuf->magic = 0x123456;
	pssbuf->datasize = uiSize;
	pssbuf->phead = NULL;
	pssbuf->frameaddr = NULL;
	pssbuf->pdata = (char *)pssbuf + sizeof(SC_ssbuff);
	pssbuf->position = 0;
	pssbuf->uiValidLen = 0;
	pssbuf->uiPutFrameCount = 0;
	pssbuf->uiSortTime = uiSortTime;
	pssbuf->uiSortTimeBak = uiSortTime;
	if(uiMaxSortTime < DEFAULT_SORT_TIME)
	{
		pssbuf->uiMaxSortTime = DEFAULT_SORT_TIME;
	}
	else
	{
		pssbuf->uiMaxSortTime = uiMaxSortTime;
	}
	pssbuf->maxtimestamp = 0;
	pssbuf->mintimestamp = 0;
	pssbuf->u32PopFrameCount = 0;
	pssbuf->u32LateFrameCount = 0;
	pssbuf->u32EarlyFrameCount = 0;
	pssbuf->freesize = uiSize;
	pssbuf->freetime = time(NULL);
	pssbuf->analytime = time(NULL);
	return 0;
}

void *pCreateStreamSort(RegisterSortInfo *info)
{
	if(!info || info->uiSize < MIM_SORT_BUFF_LEN || !info->Datacallback)
	{
		return NULL;
	}
	unsigned int len = info->uiSize+sizeof(SC_ssbuff);
	SC_ssbuff *pssbuf = (SC_ssbuff *)calloc(1, len);
	DefaultStreamSortBuff(pssbuf, info->uiSize, info->uiSortTime, info->uiMaxSortTime);
	pssbuf->puser = info->puser;
	dstr_copy_dstr(&pssbuf->name, &info->name);
	dstr_copy_dstr(&pssbuf->userid, &info->userid);
	pssbuf->Datacallback = info->Datacallback;
	return (void *)pssbuf;	
}

int FillTheNode(SC_ssnode *nd, char *position, unsigned int frame_len, unsigned long long timestamp, char frametype, int priority)
{
	nd->frametype = frametype;
	nd->len = frame_len;
	nd->position = position;
	nd->timestamp = timestamp;
    nd->priority = priority;
	nd->ucValidFlag = 1;
	return 0;
}

unsigned long long InsertSortMiddle(SC_ssnode *head, SC_ssnode *nd)
{
	SC_ssnode *p = head;
	unsigned long long alatime = 0;
	while(p)
	{
		if(p->timestamp <= nd->timestamp)
		{
			p = p->next;
			continue;
		}
		alatime = p->prev->timestamp;
		nd->next = p;
		nd->prev = p->prev;
		p->prev->next = nd;
		p->prev = nd;
		break;
	}
	return alatime;
}

int InsertSortEnd(SC_ssnode *head, SC_ssnode *nd)
{
	SC_ssnode *p = head;
	while(p->next)
	{
		p = p->next;
	}
	nd->next = NULL;
	nd->prev = p;
	p->next = nd;
	
	return 0;
}

int FreeSortNode(SC_ssnode *head, SC_ssnode *nd)
{
	SC_ssnode *p = head;	
	while(p)
	{
		if(p == nd)
		{
			
		}
		p = p->next;
	}
	return 0;
}

unsigned int MemcpyToSortBuff(SC_ssbuff *ssbuf, char *pframe, unsigned int frame_len)
{
	unsigned int position = ssbuf->position;
	unsigned int wpos = ssbuf->position;
    //_printd("data: data[-3]:%02x, data[-2]:%02x, data[-1]:%02x, data[0]:%02x, data[1]:%02x\n",
    //    pframe[-3], pframe[-2], pframe[-1], pframe[0], pframe[1]);
	if(position + frame_len <= ssbuf->datasize)
	{
		memcpy(ssbuf->pdata + position, pframe, frame_len);
		position += frame_len;
		if(position == ssbuf->datasize)
		{
			position = 0;
		}
	}
	else
	{
#if 0
		int left = ssbuf->datasize - position;
		int len = frame_len - left;
		memcpy(ssbuf->pdata + position, pframe, left);
		memcpy(ssbuf->pdata, pframe + left, len);
		position = len;
#endif
		if (frame_len > position) {
			_printd("-------------->>> memory error !\n");
			assert(frame_len < position);
		}
		memcpy(ssbuf->pdata, pframe, frame_len);
		position = frame_len;
		wpos = 0;
	}

	ssbuf->position = position;
	return wpos;
}

void CleanSortBuff(SC_ssbuff *pssbuf)
{
	SC_ssnode *p = pssbuf->phead;
	SC_frameaddr *ar = pssbuf->frameaddr;
	SC_frameaddr *nar;
	SC_ssnode *n;
	while(p && ar)
	{
		n = p->next;
		bfree(p);
		p = n;

		nar = ar->next;
		bfree(ar);
		ar = nar;
	}

	pssbuf->phead = NULL;
	pssbuf->frameaddr = NULL;
	pssbuf->position = 0;
	pssbuf->uiValidLen = 0;
	pssbuf->uiPutFrameCount = 0;
	pssbuf->maxtimestamp = 0;
	pssbuf->mintimestamp = 0;
	pssbuf->premintimestamp = 0;
	pssbuf->u32PopFrameCount = 0;
	pssbuf->u32LateFrameCount = 0;
	pssbuf->u32EarlyFrameCount = 0;
	pssbuf->uiAnalyTime = 0;
	pssbuf->freesize = pssbuf->datasize;
	pssbuf->freetime = time(NULL);
	pssbuf->analytime = time(NULL);
	pssbuf->uiSortTime = pssbuf->uiSortTimeBak;
	pssbuf->uiMaxSortTime = pssbuf->uiMaxSortTime;
	
	return;
}
void *RemallocSortBuff(SC_ssbuff *ssbuf, unsigned int uiSize)
{
	unsigned int uiSortTime = ssbuf->uiSortTime;
	
	RegisterSortInfo info = {};
	info.Datacallback = ssbuf->Datacallback;
	info.puser = ssbuf->puser;
	info.uiSize = uiSize;
	info.uiSortTime = uiSortTime;
	info.uiMaxSortTime = ssbuf->uiMaxSortTime;
	dstr_copy_dstr(&info.name, &ssbuf->name);
	dstr_copy_dstr(&info.userid, &ssbuf->userid);
	void *buff = pCreateStreamSort(&info);
	if(!buff)
	{
		return NULL;
	}
	SC_ssbuff *newssbuf = (SC_ssbuff *)buff;
	newssbuf->phead = ssbuf->phead;
	char *newpdata = newssbuf->pdata;
	unsigned int position = 0;
	SC_ssnode *pnode = ssbuf->phead;
	SC_frameaddr *frameaddr = ssbuf->frameaddr;
	while(pnode && frameaddr)
	{
		char *end = ssbuf->pdata + ssbuf->datasize;
		if(pnode->position + pnode->len > end)
		{
			int left = end - pnode->position;
			int len = pnode->len - left;
			memcpy(newpdata + position, pnode->position, left);
			memcpy(newpdata + position+left, ssbuf->pdata, len);
		}
		else
		{
			memcpy(newpdata + position, pnode->position, pnode->len);
		}
		pnode->position = newpdata + position;

		frameaddr->position = position;
		frameaddr = frameaddr->next;
		
		position += pnode->len;
		pnode = pnode->next;
	}

	newssbuf->position = position;
	newssbuf->uiPutFrameCount = ssbuf->uiPutFrameCount;
	newssbuf->uiValidLen = ssbuf->uiValidLen;
	newssbuf->maxtimestamp = ssbuf->maxtimestamp;
	newssbuf->mintimestamp = ssbuf->mintimestamp;
	newssbuf->u32EarlyFrameCount = ssbuf->u32EarlyFrameCount;
	newssbuf->u32LateFrameCount = ssbuf->u32LateFrameCount;
	newssbuf->u32PopFrameCount = ssbuf->u32PopFrameCount;
	newssbuf->uiPutFrameCount = ssbuf->uiPutFrameCount;
	newssbuf->frameaddr = ssbuf->frameaddr;
	newssbuf->uiAnalyTime = ssbuf->uiAnalyTime;
	newssbuf->freesize = uiSize - position;
	newssbuf->uiSortTimeBak = ssbuf->uiSortTimeBak;
	newssbuf->freetime = time(NULL);
	newssbuf->analytime = time(NULL);
	bfree(ssbuf);
	return buff;
}


int CFrameaddrList(void **had, unsigned int iposition, unsigned int oposition, int type)
{
	SC_frameaddr *h = (SC_frameaddr *)(*had);
	SC_frameaddr *ad = h;
	if(0 == type)//only add
	{
		SC_frameaddr *fd = (SC_frameaddr *)calloc(1, sizeof(SC_frameaddr));
		if(!fd)
		{
			return -1;
		}
		fd->position = iposition;
		if(!ad)
		{
			*had = fd;
			fd->next = fd;
			fd->prev = fd;
			return 0;
		}
		
		ad = h->prev;
		
		fd->next = h;
		h->prev = fd;
		fd->prev = ad;
		ad->next = fd;
	}
	else if(1 == type)//modify the node,and move to the end
	{
		SC_frameaddr *fd = NULL;
		SC_frameaddr *node = ad;
		do
		{
			if(node->position == oposition && !fd)
			{
				fd = node;
				fd->position = iposition;
				break;
			}
			node = node->next;
		}while(h != node);
		if(!fd)
		{
			return -2;
		}

		if(fd == h)//head
		{
			*had = h = fd->next;
		}
		else if(h->prev != fd)//middle,so move the node to the end
		{
			node->prev->next = node->next;
			node->next->prev = node->prev;
			
			h->prev->next = fd;
			fd->prev = h->prev;
			h->prev = fd;
			fd->next = h;
		}
		else//If this is the last one, keep it
		{
		}
	}
	else//2 == type: only delete
	{
		SC_frameaddr *fd = NULL;
		SC_frameaddr *node = ad;
		do
		{
			if(node->position == oposition && !fd)
			{
				fd = node;
				node->prev->next = node->next;
				node->next->prev = node->prev;
				break;
			}
			node = node->next;
		}while(h != node);

		if(!fd)
		{
			return -2;
		}
		if(fd == h)
		{
			*had = h = fd->next;
		}
		
		bfree(fd);
	}

	return 0;
}

void printfsort(SC_ssbuff *ssbuf)
{
	_printd("buff_name=%s, userid=%s; \ndatasize=%u\n pdata=[%p]\n position=%u\n"
			"maxtimestamp=%llu\n mintimestamp=%llu\n"
			"uiPutFrameCount=%u\n u32PopFrameCount=%u\n"
			"uiValidLen=%u\n uiSortTime=%u\n u32EarlyFrameCount=%u\n"
			"u32LateFrameCount=%u\n freesize=%u\n",	
	ssbuf->name.array, ssbuf->userid.array,		
	ssbuf->datasize, ssbuf->pdata, ssbuf->position, 
	ssbuf->maxtimestamp, ssbuf->mintimestamp,
	ssbuf->uiPutFrameCount, ssbuf->u32PopFrameCount,
	ssbuf->uiValidLen, ssbuf->uiSortTime, ssbuf->u32EarlyFrameCount,
	ssbuf->u32LateFrameCount, ssbuf->freesize);

#if 0
	printf("\n#############################start###############################\n\n");
	SC_ssnode *phead = ssbuf->phead;
	while(phead)
	{
		printf("position=%u timestamp=%llu len=%d\n", (unsigned int)(phead->position-ssbuf->pdata), phead->timestamp, phead->len);
		phead = phead->next;
	}
	printf("\n============================================================\n\n");
	SC_frameaddr *frameaddr = ssbuf->frameaddr;
	do
	{
		printf("position=%u\n", frameaddr->position);
		frameaddr = frameaddr->next;
	}while(ssbuf->frameaddr != frameaddr);
	
	printf("\n#############################end###############################\n\n");
#endif

}

int CheckStreambuff(void **agrv, unsigned int frame_len)
{
	int ret = 0;
	SC_ssbuff *ssbuf = (SC_ssbuff *)*agrv;
	if(!ssbuf->frameaddr || ssbuf->uiPutFrameCount <= 2)
	{
		return 0;
	}

	unsigned int earliest = ssbuf->frameaddr->position;
	unsigned int willposi_s = ssbuf->position;
	unsigned int willposi_e = willposi_s + frame_len;
	unsigned int datasize = 0, freesize = 0;
	int tooloog = 0;
	if(willposi_e <= ssbuf->datasize)
	{
		if(earliest >= willposi_s && earliest <= willposi_e)
		{
			_printd("earliest=%u willposi_s=%u willposi_e=%u", earliest, willposi_s, willposi_e);
			blog(MGW_LOG_INFO, "earliest=%u willposi_s=%u willposi_e=%u", earliest, willposi_s, willposi_e);
			tooloog = 1;//check buff too small
		}
	}
	else
	{
#if 0
		willposi_e -= ssbuf->datasize;
		if(earliest >= willposi_s || earliest <= willposi_e)
		{
			_printd("===========earliest=%u willposi_s=%u willposi_e=%u", earliest, willposi_s, willposi_e);
			tooloog = 1;
		}
#endif
		willposi_e = frame_len;
		if(earliest >= willposi_s || earliest <= willposi_e)
		{
			_printd("earliest=%u willposi_s=%u willposi_e=%u", earliest, willposi_s, willposi_e);
			blog(MGW_LOG_INFO, "earliest=%u willposi_s=%u willposi_e=%u", earliest, willposi_s, willposi_e);
			tooloog = 1;//check buff too small
		}

		unsigned long long tdef = time(NULL);
		//check AnalyTime
		if(tdef - ssbuf->analytime >= 5*60)
		{
			_printd("buff_name=%s, userid=%s; uiAnalyTime=====%u", ssbuf->name.array, ssbuf->userid.array, ssbuf->uiAnalyTime);
			blog(MGW_LOG_INFO, "buff_name=%s, userid=%s; uiAnalyTime=%u, now uiSortTime=%u", ssbuf->name.array, ssbuf->userid.array, ssbuf->uiAnalyTime, ssbuf->uiSortTime);
			if(ssbuf->uiAnalyTime < ssbuf->uiSortTime && ssbuf->uiAnalyTime > 0)
			{
				unsigned int defTime = ssbuf->uiSortTime - ssbuf->uiAnalyTime;
				if(defTime > 200000)
				{
					ssbuf->uiSortTime -= 200000;//һ������200ms 
				}
				if(defTime > 100000)
				{
					ssbuf->uiSortTime -= 100000;//һ������100ms 
				}
				else
				{
					ssbuf->uiSortTime = ssbuf->uiAnalyTime;
				}

				if(ssbuf->uiSortTime < ssbuf->uiSortTimeBak)
				{
					ssbuf->uiSortTime = ssbuf->uiSortTimeBak;
				}
			}
			ssbuf->uiAnalyTime = 0;
			ssbuf->analytime = tdef;//���¼���ʱ��
		}

		//check buff too big
		if(!tooloog && tdef - ssbuf->analytime >= 15*60)
		{
			ssbuf->freetime = tdef;
			unsigned int newsize = ssbuf->datasize/2;
			if(MIM_SORT_BUFF_LEN < newsize && ssbuf->freesize > newsize)//һ�����
			{
				_printd("buff is too big, will remalloc, freesize=%u, %u", ssbuf->freesize, newsize);
				blog(MGW_LOG_INFO, "buff_name=%s, userid=%s; buff is too big, will remalloc, freesize=%u, %u",
                            ssbuf->name.array, ssbuf->userid.array, ssbuf->freesize, newsize);
				void *rf = RemallocSortBuff(ssbuf, newsize);
				if(rf)
				{
					*agrv = rf;
					ssbuf = (SC_ssbuff *)*agrv;
					ret = 1;
				}
			}
#if 0
			unsigned int newsize = 0;
			if(earliest < willposi_e)
			{
				datasize = ssbuf->datasize - willposi_e + earliest;
				newsize = ssbuf->datasize/2;
				if(MIM_SORT_BUFF_LEN < newsize && datasize > newsize)//һ�����
				{
					_printd("buff is too big, will remalloc, %u", newsize);
					void *rf = RemallocSortBuff(ssbuf, newsize);
					if(rf)
					{
						*agrv = rf;
						ssbuf = (SC_ssbuff *)*agrv;
						ret = 1;
					}
				}
			}
			else if(earliest > willposi_e)
			{
				datasize = earliest - willposi_e;
				newsize = ssbuf->datasize/2;
				if(MIM_SORT_BUFF_LEN < newsize && datasize > newsize)//һ�����
				{
					_printd("buff is too big, will remalloc, %u", newsize);
					void *rf = RemallocSortBuff(ssbuf, newsize);
					if(rf)
					{
						*agrv = rf;
						ssbuf = (SC_ssbuff *)*agrv;
						ret = 1;
					}
				}
			}
#endif
		}
	}
	if(tooloog)//check buff too small
	{
		if(ssbuf->datasize > MAX_SORT_BUFF_LEN)
		{
			_printd("Address to repeat;will clean buff");
			blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; Address to repeat;Valid data [%u], will clean buff",
                        ssbuf->name.array, ssbuf->userid.array, ssbuf->uiValidLen);
			printfsort(ssbuf);
			CleanSortBuff(ssbuf);
		}
		else
		{
			datasize = ssbuf->datasize*3/2;
			_printd("Address to repeat;Valid data [%u], will remalloc, %u:%u", ssbuf->uiValidLen, datasize, ssbuf->datasize);
			blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; Address to repeat;Valid data [%u], will remalloc, %u:%u",
                            ssbuf->name.array, ssbuf->userid.array, ssbuf->uiValidLen, datasize, ssbuf->datasize);
			printfsort(ssbuf);
			void *rf = RemallocSortBuff(ssbuf, datasize);
			if(!rf)
			{
				_printd("Address to repeat, will clean buff");
				blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; Address to repeat, will clean buff", ssbuf->name.array, ssbuf->userid.array);
				printfsort(ssbuf);
				CleanSortBuff(ssbuf);
			}
			*agrv = rf;
			ssbuf = (SC_ssbuff *)*agrv;
			ret = 1;
		}
	}
	else
	{
		freesize = ssbuf->freesize;
		if(earliest < willposi_e)
		{
			freesize = ssbuf->datasize - willposi_e + earliest;
		}
		else if(earliest > willposi_e)
		{
			freesize = earliest - willposi_e;
		}

		if(ssbuf->freesize > freesize)
		{
			ssbuf->freesize = freesize;
		}

	}
	return ret;
}

void DelectStreamSort(void *agrv)
{
	SC_ssbuff *ssbuf = (SC_ssbuff *)agrv;
	CleanSortBuff(ssbuf);
    dstr_free(&ssbuf->name);
    dstr_free(&ssbuf->userid);
	
	bfree(ssbuf);
}

void CleanStreamSort(void *agrv)
{
	SC_ssbuff *ssbuf = (SC_ssbuff *)agrv;
	blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; clean stream sort", ssbuf->name.array, ssbuf->userid.array);
	CleanSortBuff(ssbuf);
}

int PopFrameStreamSort(SC_ssbuff *ssbuf)
{
	sc_sortframe oframe;
	SC_ssnode *nd = NULL;
	unsigned int timedef = 0;
	unsigned int oposition = 0;
	int i = 0, ret = 0;
	while(i++ < 2)
	{
		if(ssbuf->uiPutFrameCount - ssbuf->u32PopFrameCount <= 3)
		{
			break;
		}
		
		timedef = ssbuf->maxtimestamp - ssbuf->mintimestamp;
		if(timedef > ssbuf->uiSortTime)
		{
			nd = ssbuf->phead;
			oposition = nd->position - ssbuf->pdata;
			if((ret = CFrameaddrList((void **)&ssbuf->frameaddr, 0, oposition, 2)) < 0)//�����ַList��Ӧ�ڵ�
			{
				_printd("CFrameaddrList return err:%d", ret);
				blog(MGW_LOG_INFO,"CFrameaddrList return err:%d", ret);
				if(ret < 0)
				{
					break;
				}
			}
			oframe.frame = nd->position;
			oframe.frame_len = nd->len;
			oframe.frametype = nd->frametype;
			oframe.timestamp = nd->timestamp;
            oframe.priority  = nd->priority;
			ssbuf->premintimestamp = nd->timestamp;
			ret = ssbuf->Datacallback(ssbuf->puser, &oframe);
			if(ret < 0)
			{
				return ret;
			}
			ssbuf->uiValidLen -= nd->len;
			ssbuf->phead = nd->next;
			ssbuf->u32PopFrameCount++;
			if(nd == nd->prev)
			{
				ssbuf->phead->prev = ssbuf->phead;
			}
			else
			{
				ssbuf->phead->prev = nd->prev;//ssbuf->phead;
			}
			//nd->prev->next = ssbuf->phead;
			nd->position = NULL;
			bfree(nd);
			
			ssbuf->mintimestamp = ssbuf->phead->timestamp;
		}
		else
		{
			break;
		}
	}

	return 0;
}

int PutFrameStreamSort(void **agrv, sc_sortframe *piframe)
{
	SC_ssbuff *ssbuf = (SC_ssbuff *)*agrv;
	sc_sortframe *iframe = piframe;	
	int ret = 0, revl = 0;
	SC_ssnode *nd = NULL;

	unsigned int position = 0;
	unsigned int timedef = 0;
	unsigned long long alatime = 0;
	if(iframe->frame_len >= (ssbuf->datasize/2))
	{
		_printd("buff_name=%s, userid=%s; the frame is too big, len=%d/%d", ssbuf->name.array, ssbuf->userid.array, iframe->frame_len, ssbuf->datasize);
		blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; the frame is too big, len=%d/%d", ssbuf->name.array, ssbuf->userid.array, iframe->frame_len, ssbuf->datasize);
		return -1;
	}

	if(CheckStreambuff(agrv, iframe->frame_len))
	{
		ssbuf = (SC_ssbuff *)*agrv;
	}
	
	if(!ssbuf->phead)
	{
		nd = (SC_ssnode*)calloc(1, sizeof(SC_ssnode));
		if(!nd)
		{
			return -1;
		}
		//ssbuf->position = iframe->frame_len;
		ssbuf->uiPutFrameCount = 1;
		ssbuf->uiValidLen = iframe->frame_len;
		ssbuf->maxtimestamp = iframe->timestamp;
		ssbuf->mintimestamp = iframe->timestamp;
		ssbuf->premintimestamp = ssbuf->mintimestamp;
		ssbuf->phead = nd;
		MemcpyToSortBuff(ssbuf, iframe->frame, iframe->frame_len);
		FillTheNode(nd, ssbuf->pdata, iframe->frame_len, iframe->timestamp, iframe->frametype, iframe->priority);
		nd->prev = nd;
		nd->next = NULL;
		if((revl = CFrameaddrList((void **)&ssbuf->frameaddr, 0, 0, 0)) < 0)
		{
			_printd("buff_name=%s, userid=%s; CFrameaddrList return err:%d", ssbuf->name.array, ssbuf->userid.array, revl);
			blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; CFrameaddrList return err:%d", ssbuf->name.array, ssbuf->userid.array, revl);
			if(revl < 0)
			{
				ssbuf->phead = NULL;
				bfree(nd);
				return -1;
			}
		}
		return 0;
	}

	if(iframe->timestamp < ssbuf->maxtimestamp)
	{
		timedef = ssbuf->maxtimestamp - iframe->timestamp;
 		if(timedef > ssbuf->uiMaxSortTime)
 		{
			ssbuf->u32LateFrameCount++;
			ssbuf->u32LateFrameDrop++;
			if(0 == (ssbuf->u32LateFrameCount % 50))
			{
				blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; time:%llu is too late!! %llu -%llu=%u is larger than %d, drop %u", 
								ssbuf->name.array, ssbuf->userid.array, iframe->timestamp, ssbuf->maxtimestamp,
								iframe->timestamp, timedef, ssbuf->uiMaxSortTime, ssbuf->u32LateFrameCount);
				_printd("buff_name=%s, userid=%s; time:%llu is too late!! %llu -%llu=%u is larger than %d, drop %u",
								ssbuf->name.array, ssbuf->userid.array, iframe->timestamp, ssbuf->maxtimestamp, 
								iframe->timestamp, timedef, ssbuf->uiMaxSortTime, ssbuf->u32LateFrameCount);
			}
			if(ssbuf->u32LateFrameDrop > 60)
			{
				CleanStreamSort(ssbuf);
			}
			return -1;
		}
		else
		{
			ssbuf->u32LateFrameDrop = 0;
		}
		//��ֹʱ�䷴ת
		if(ssbuf->premintimestamp > iframe->timestamp)
		{
			ssbuf->u32LateFrameCount++;
			ssbuf->u32LateFrameDrop++;
 			_printd("buff_name=%s, userid=%s; time:%llu is too late!! premin=%llu;, drop %u",
                    ssbuf->name.array, ssbuf->userid.array, iframe->timestamp, ssbuf->premintimestamp, ssbuf->u32LateFrameCount);
#if 0
			tlog(TMGW_LOG_ERROR, "buff_name=%s, userid=%s; time:%llu is too late!! premin=%llu;, drop %u", ssbuf->name, ssbuf->userid, iframe->timestamp, ssbuf->premintimestamp, \
				ssbuf->u32LateFrameCount);
#endif

			if(iframe->timestamp < ssbuf->mintimestamp)
			{
				//��������ʱ��
				timedef = ssbuf->mintimestamp - iframe->timestamp;
				if(ssbuf->uiSortTime + timedef < ssbuf->uiMaxSortTime)
				{
					if(timedef > 60000)
					{
						timedef = 20000;
					}
					ssbuf->uiSortTime += timedef;
					//ssbuf->uiAnalyTime = ssbuf->uiSortTime;
					unsigned long long nowtime = time(NULL);
					_printd("buff_name=%s, userid=%s; Delay increase,now uiSortTime=%u us, will add %u us", 
						ssbuf->name.array, ssbuf->userid.array, ssbuf->uiSortTime, timedef);
					if(nowtime - ssbuf->analytime >= 2)//��ֹ��־����
					{
						blog(MGW_LOG_INFO, "buff_name=%s, userid=%s; Delay increase,now uiSortTime=%u us, will add %u us", 
							ssbuf->name.array, ssbuf->userid.array, ssbuf->uiSortTime, timedef);
					}
					ssbuf->analytime = nowtime;//���¼���ʱ��
					ssbuf->uiAnalyTime = 0;
				}
			}
			else
			{
				blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s;the sort err, premin=%llu, nowmin=%llu, recvtime=%llu", \
					ssbuf->name.array, ssbuf->userid.array, ssbuf->premintimestamp, ssbuf->mintimestamp, iframe->timestamp);
			}
			return -1;
		}
		
		nd = (SC_ssnode*)calloc(1, sizeof(SC_ssnode));
		if(!nd)
		{
			return -1;
		}
		position = MemcpyToSortBuff(ssbuf, iframe->frame, iframe->frame_len);		
		FillTheNode(nd, ssbuf->pdata + position, iframe->frame_len, iframe->timestamp, iframe->frametype, iframe->priority);
		if((revl = CFrameaddrList((void **)&ssbuf->frameaddr, position, 0, 0)) < 0)
		{
			_printd("buff_name=%s, userid=%s; CFrameaddrList return err:%d", ssbuf->name.array, ssbuf->userid.array, revl);
			blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; CFrameaddrList return err:%d", ssbuf->name.array, ssbuf->userid.array, revl);
			if(revl < 0)
			{
				bfree(nd);
				return -1;
			}
		}	
		
		ssbuf->uiValidLen += iframe->frame_len;
		ssbuf->uiPutFrameCount++;
		if(iframe->timestamp < ssbuf->mintimestamp)
		{
			//��������ʱ��
			timedef = ssbuf->mintimestamp - iframe->timestamp;
			if(ssbuf->uiSortTime + timedef < ssbuf->uiMaxSortTime)
			{
				if(timedef > 60000)
				{
					timedef = 20000;
				}
				ssbuf->uiSortTime += timedef;
				//ssbuf->uiAnalyTime = ssbuf->uiSortTime;
				unsigned long long nowtime = time(NULL);
				_printd("buff_name=%s, userid=%s; Delay increase,uiSortTime=%u us, timedef=%u us", 
					ssbuf->name.array, ssbuf->userid.array, ssbuf->uiSortTime, timedef);
				if(nowtime - ssbuf->analytime >= 2)//��ֹ��־����
				{
					blog(MGW_LOG_INFO, "buff_name=%s, userid=%s; Delay increase,now uiSortTime=%u us, will add timedef=%u us", 
						ssbuf->name.array, ssbuf->userid.array, ssbuf->uiSortTime, timedef);
				}
				ssbuf->analytime = nowtime;//���¼���ʱ��
				ssbuf->uiAnalyTime = 0;
			}
			
			nd->prev = ssbuf->phead->prev;
			nd->next = ssbuf->phead;
			ssbuf->phead->prev = nd;
			ssbuf->phead = nd;
			ssbuf->mintimestamp = iframe->timestamp;
		}
		else
		{
			//mintimestamp < timestamp < maxtimestamp
			nd->timestamp = iframe->timestamp;
			alatime = InsertSortMiddle(ssbuf->phead, nd);
			if(alatime)
			{
				timedef = ssbuf->maxtimestamp - alatime;
				if(ssbuf->uiAnalyTime < timedef)
				{
					ssbuf->uiAnalyTime = timedef;
				}
			}
		}

//		_printd("max-min=%llu", ssbuf->maxtimestamp-ssbuf->mintimestamp);
		//printfsort(ssbuf);
		return 0;
	}
	else
	{
		timedef = iframe->timestamp - ssbuf->maxtimestamp;
 		if(timedef >= ssbuf->uiMaxSortTime)//ʱ����������
 		{
			ssbuf->u32EarlyFrameCount++;
			ssbuf->u32EarlyFrameDrop++;
			
			if(0 == (ssbuf->u32EarlyFrameCount % 50))
			{
	 			_printd("buff_name=%s, userid=%s; time:%llu is too early !! %llu - %llu=%u, drop %u", 
					ssbuf->name.array, ssbuf->userid.array, iframe->timestamp, iframe->timestamp, ssbuf->mintimestamp, \
					timedef, ssbuf->u32EarlyFrameCount);
				blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; time:%llu is too early !! %llu - %llu=%u, drop %u", 
					ssbuf->name.array, ssbuf->userid.array, iframe->timestamp, iframe->timestamp, ssbuf->mintimestamp, \
					timedef, ssbuf->u32EarlyFrameCount);
			}
			//printfsort(ssbuf);
			if(ssbuf->u32EarlyFrameDrop > 60 || timedef > 10000000)
			{
				CleanStreamSort(ssbuf);
				ssbuf->u32EarlyFrameDrop = 0;
				return -1;
			}
//			return -1;
		}
		else
		{
			ssbuf->u32EarlyFrameDrop = 0;
		}
		position = MemcpyToSortBuff(ssbuf, iframe->frame, iframe->frame_len);//��Ҫ����ƶ�λ��
		nd = ssbuf->phead;
		timedef = iframe->timestamp - ssbuf->mintimestamp;//Ŀǰ���������ʱ���
		if(timedef > ssbuf->uiSortTime && nd && nd->next)//�������Ҫ��
		{
//			_printd("max-min=%llu", timedef);
			unsigned int oposition = nd->position - ssbuf->pdata;
			if((revl = CFrameaddrList((void **)&ssbuf->frameaddr, position, oposition, 1)) < 0)
			{
				_printd("buff_name=%s, userid=%s; oposition=%u; CFrameaddrList return err:%d", ssbuf->name.array, ssbuf->userid.array,oposition, revl);
				blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; oposition=%u; CFrameaddrList return err:%d", ssbuf->name.array, ssbuf->userid.array,oposition, revl);
				if(revl < 0)
				{
					return -1;
				}
			}

			sc_sortframe oframe;
			oframe.frame = nd->position;
			oframe.frame_len = nd->len;
			oframe.frametype = nd->frametype;
			oframe.timestamp = nd->timestamp;
            oframe.priority  = nd->priority;
			ssbuf->premintimestamp = nd->timestamp;
			ret = ssbuf->Datacallback(ssbuf->puser, &oframe);
			if(ret < 0)
			{
				_printd("buff_name=%s, userid=%s; Datacallback return err:%d", ssbuf->name.array, ssbuf->userid.array,ret);
				blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; Datacallback return err:%d", ssbuf->name.array, ssbuf->userid.array,ret);
				//return ret;
			}
			
			ssbuf->uiValidLen -= nd->len;
			ssbuf->phead = nd->next;
			ssbuf->u32PopFrameCount++;

			nd->position = NULL;
			ssbuf->mintimestamp = ssbuf->phead->timestamp;
			
			ret = 1;
		}
		else
		{
			nd = (SC_ssnode*)calloc(1, sizeof(SC_ssnode));
			if(!nd)
			{
				return -1;
			}
			ret = 0;
			if((revl = CFrameaddrList((void **)&ssbuf->frameaddr, position, 0, 0)) < 0)
			{
				_printd("buff_name=%s, userid=%s; CFrameaddrList return err:%d", ssbuf->name.array, ssbuf->userid.array,revl);
				blog(MGW_LOG_ERROR, "buff_name=%s, userid=%s; CFrameaddrList return err:%d", ssbuf->name.array, ssbuf->userid.array,revl);
				if(revl < 0)
				{
					bfree(nd);
					return -1;
				}
			}
		}
		ssbuf->uiValidLen += iframe->frame_len;
		//position = ssbuf->position - iframe->frame_len;
		FillTheNode(nd, ssbuf->pdata + position, iframe->frame_len, iframe->timestamp, iframe->frametype, iframe->priority);
		InsertSortEnd(ssbuf->phead, nd);
		ssbuf->uiPutFrameCount++;
		ssbuf->maxtimestamp = iframe->timestamp;
		
		if(0 == ssbuf->uiPutFrameCount%1000)
		{
			printfsort(ssbuf);
		}

		if(ret > 0)
		{
			ret = PopFrameStreamSort(ssbuf);
		}
		return ret;
	}
	
}
