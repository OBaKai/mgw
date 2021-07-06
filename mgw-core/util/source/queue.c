/** Jovial Young 2020/02/30 **/

#include "c99defs.h"
#include "queue.h"
#include "bmem.h"
#include <malloc.h>

node_t *gen_node(int action, void *data, void *priv_data)
{
    node_t *n = (node_t*)bzalloc(sizeof(node_t));
    if (!!n) {
        n->action = action;
        n->delay = 0;
        n->data = data;
        n->priv_data = priv_data;
        n->next = NULL;
    } else {
        n = NULL;
    }
    return n;
}

void del_node(node_t* n)
{
    if (!!n) {
        n->action = 0;
        bfree(n->data);
        //P_FREE(n->priv_data);
        n->next = NULL;
        bfree(n);
    }
}

queue_t *q_init(void)
{
    queue_t *q = NULL;
    q = (queue_t*)bzalloc(sizeof(queue_t));
    q->head = q->tail = (node_t*)bzalloc(sizeof(node_t));
    q->head->next = NULL;
    q->length = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    return q;
}

size_t q_enqueue(queue_t *q, node_t *n)
{
    size_t q_len = 0;
    if (q && n) {
        pthread_mutex_lock(&q->mutex);
        //FIFO��add the new node to tail
        q->tail->next = n;
        //set the new node be tail
        q->tail = n;
        q_len = ++q->length;
        pthread_cond_signal(&q->cond);
        pthread_mutex_unlock(&q->mutex);
    }
    return q_len;
}

node_t *q_dequeue(queue_t *q)
{
    node_t *n = NULL;
    if (q) {
        if (q->head != q->tail && q->length > 0) {
            pthread_mutex_lock(&q->mutex);
            //get the head node
            n = q->head->next;
            q->head->next = n->next;
            if (q->tail == n)
                q->tail = q->head;
            --q->length;
            pthread_mutex_unlock(&q->mutex);
        }
    }
    return n;
}

inline size_t q_lenght(queue_t *q)
{
    return !!q ? q->length : 0;
}

bool q_empty(queue_t *q)
{
    return !!q ? (q->length == 0 && q->head == q->tail) : true;
}

void q_clear(queue_t *q)
{
    if (q) {
        while(!q_empty(q))
            del_node(q_dequeue(q));
    }
}

void q_destroy(queue_t *q)
{
    if (q) {
        q_clear(q);
        bfree(q->head);
        bfree(q);
    }
}
