#include "list_function.h"

#define _printd(fmt, ...)	printf ("[%s][%d]"fmt"\n", (char *)strrchr(__FILE__, '\\')?(strrchr(__FILE__, '/') + 1):__FILE__, __LINE__, ##__VA_ARGS__)

SC_node *list_init()
{
	SC_node *p = (SC_node *)calloc(1, sizeof(SC_node));
	if(!p)
		return NULL;

	p->next = NULL;
	p->prev = NULL;
	p->pbuff = NULL;
	p->uid = -1;
	p->buflen = 0;
	snprintf(p->name, sizeof(p->name), "%s", "list_head");
    return p;
}

int list_en(SC_node *phead, char *pbuff, char *name)
{
	if(NULL == phead || NULL == pbuff)
    {
		return Q_false;
    }
	SC_node *p = (SC_node *)malloc(sizeof(SC_node));
	if(NULL == p)
	{
		return Q_false;
	}
	p->pbuff = pbuff;
	snprintf(p->name, sizeof(p->name), "%s", name);

	SC_node *pnode = phead;
	while(NULL != pnode->next)
	{	
		pnode = (SC_node *)pnode->next;
	}
	
	if(NULL == pnode->next)
	{
		pnode->next = p;
		p->prev = pnode;
		p->next = NULL;
	}
	else
	{
		free(p);
		return Q_false;
	}
		
    return Q_true;
}

SC_node *list_de(SC_node *phead, char **pbuff, char *name)
{
	if(NULL == name || NULL == phead)
    {
		return Q_false;
    }
	
    SC_node *pnode = phead;
	while(NULL != pnode)
	{
		if(!strncmp(pnode->name, name, sizeof(pnode->name)))
		{
			*pbuff = pnode->pbuff;
			return pnode;
		}
		pnode = (SC_node *)pnode->next;
	}
    return NULL;
}


int list_free(SC_node *phead, char *name)
{
    SC_node *pnode = phead;
	while(NULL != pnode)
	{	
		if(!strncmp(pnode->name, name, sizeof(pnode->name)))
		{
			// List header
			if(!pnode->prev)
			{
				phead = pnode->next;
				if(phead)
				{
					phead->prev = NULL;
				}
			}
			/* List tail */	
			else if(!pnode->next)
			{
				pnode->prev->next = NULL;
			}
			else
			{
				pnode->prev->next = pnode->next;
				pnode->next->prev = pnode->prev;
			}
			free(pnode);
			return Q_true;
		}
		pnode = (SC_node *)pnode->next;
	}
    return Q_false;
}


int list_delect2(SC_node *phead)
{
    SC_node *pnode = phead;
	SC_node *pdel = NULL;
	while(NULL != pnode)
	{
		pdel = pnode;		
		pnode = (SC_node *)pnode->next;
		free(pdel);
	}
    return Q_true;
}

int list_en2(SC_node *phead, char *pbuff, int uid)
{
	if(NULL == phead || NULL == pbuff)
    {
		return Q_false;
    }
	SC_node *p = (SC_node *)malloc(sizeof(SC_node));
	if(NULL == p)
	{
		return Q_false;
	}
	p->pbuff = pbuff;
	p->uid = uid;
	
	SC_node *pnode = phead;
	while(NULL != pnode->next)
	{	
		pnode = (SC_node *)pnode->next;
	}
	
	if(NULL == pnode->next)
	{
		pnode->next = p;
		p->prev = pnode;
		p->next = NULL;
	}
	else
	{
		free(p);
		return Q_false;
	}
		
    return Q_true;
}

int list_de2(SC_node *phead, char **pbuff)
{
	if(NULL == phead)
    {
		return Q_false;
    }
	
    SC_node *pnode = phead;
	SC_node *del = NULL; 
	if(pnode->next)
	{
		del = pnode = pnode->next;
		*pbuff = del->pbuff;
		pnode = pnode->next;
		if(pnode)
		{
			phead->next = pnode;
			pnode->prev = phead;
		}
		else
		{
			phead->next = NULL;
		}
		free(del);
		return Q_true;
	}
	return Q_false;
}

int list_de_2(SC_node *phead, char **pbuff, int uid)
{
	if(NULL == phead)
    {
		return Q_false;
    }
	
    SC_node *pnode = phead;
	while(NULL != pnode)
	{
		if(uid == pnode->uid)
		{
			*pbuff = pnode->pbuff;
			return Q_true;
		}
		pnode = (SC_node *)pnode->next;
	}
	return Q_false;
}
int list_tra_buff(SC_node *pnode, SC_node **next)
{
	if(NULL == pnode || NULL == pnode->next)
    {
		return Q_false;
    }
	*next = pnode->next;
	return Q_true;
}

int list_free2(SC_node *phead, int uid)
{
    SC_node *pnode = phead;
	while(NULL != pnode)
	{	
		if(uid == pnode->uid)
		{
			if(!pnode->prev)
			{
				phead = pnode->next;
				if(phead)
				{
					phead->prev = NULL;
				}
			}
			else if(!pnode->next)
			{
				pnode->prev->next = NULL;
			}
			else
			{
				pnode->prev->next = pnode->next;
				pnode->next->prev = pnode->prev;
			}
			free(pnode);
			return Q_true;
		}
		pnode = (SC_node *)pnode->next;
	}
    return Q_false;
}