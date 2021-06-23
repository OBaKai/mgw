#ifndef _LIST_FUNCTION__H_
#define _LIST_FUNCTION__H_
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C"{
#endif

#define Q_true 1
#define Q_false 0

/* static node */
typedef struct _SC_node
{
    char *pbuff;
	int buflen;
	char name[64];
	int uid;
	struct _SC_node *prev;
	struct _SC_node *next;
}SC_node;

SC_node *list_init();
int list_en(SC_node *phead, char *pbuff, char *name);
/* Match node of 'name' */
SC_node * list_de(SC_node *phead, char **pbuff, char *name);
int list_free(SC_node *phead, char *name);
void list_clear(int cho);

int list_delect2(SC_node *phead);
int list_de2(SC_node *phead, char **pbuff);

int list_en2(SC_node *phead, char *pbuff, int uid);
int list_de_2(SC_node *phead, char **pbuff, int uid);
int list_free2(SC_node *phead, int uid);
int list_tra_buff(SC_node *pnode, SC_node **next);

#ifdef __cplusplus
}
#endif
#endif

