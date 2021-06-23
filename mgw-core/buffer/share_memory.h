#ifndef __SHARE_MEMORY_H__
#define __SHARE_MEMORY_H__

#ifdef __cplusplus
extern "C"{
#endif

char *CreateShareMemory(int *Fd, const char *name, int size, int frames, void *priv_data);
void DeleteShareMemory(void *head);

#ifdef __cplusplus
}
#endif
#endif
