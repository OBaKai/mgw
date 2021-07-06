#ifndef __STREAM_SORT_H__
#define __STREAM_SORT_H__
#include <stdio.h>
#include <string.h>
#include "util/dstr.h"

#ifdef __cplusplus
extern "C"{
#endif

#define _printd(fmt, ...)	printf ("[%s][%d]"fmt"\n", (char *)strrchr(__FILE__, '\\')?(strrchr(__FILE__, '/') + 1):__FILE__, __LINE__, ##__VA_ARGS__)

#define SORT_SIZE_DEF	2*1024*1024
#define SORT_TIME_DEF	20*1000

typedef struct _sc_sortframe_
{
	char *frame;
	unsigned int frame_len;
	unsigned long long timestamp;
	char frametype;
    int priority;
}sc_sortframe;

typedef int (*SortDataCallback)(void *puser, sc_sortframe *oframe);

typedef struct
{
	unsigned int uiSize;
	unsigned int uiSortTime;
	unsigned int uiMaxSortTime;
	struct dstr name;
	struct dstr userid;
	void *puser;
	SortDataCallback Datacallback;
}RegisterSortInfo;

void *pCreateStreamSort(RegisterSortInfo *info);
void DelectStreamSort(void *agrv);
void CleanStreamSort(void *agrv);
int PutFrameStreamSort(void **agrv, sc_sortframe *iframe);

#ifdef __cplusplus
}
#endif
#endif
