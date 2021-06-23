#ifndef __MALLOC_MEMORY_H__
#define __MALLOC_MEMORY_H__
#include <stdio.h>

#ifdef __cplusplus
extern "C"{
#endif

char *CreateMallocMemory(int *Fd, const char *name, int size, int frames, io_mode_t mode, void *priv_data);
int DeleteMallocMemory(const char *name);

#ifdef __cplusplus
}
#endif
#endif
