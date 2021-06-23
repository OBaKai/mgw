#ifndef _QUEUE_H_
#define _QUEUE_H_
#include "c99defs.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_REPEAT  5  //never greater than 10 repeat

typedef struct d_node {
    int action;
    int delay;
    void *data;
    void *priv_data;    //private data free by user
    struct d_node *next;
}node_t;

typedef struct d_queue {
    node_t *head, *tail;
    size_t length;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
}queue_t;

node_t *gen_node(int action, void *data, void *priv_data);
void del_node(node_t* n);
queue_t *q_init(void);
size_t q_enqueue(queue_t *q, node_t *n);
node_t *q_dequeue(queue_t *q);
size_t q_lenght(queue_t *q);
bool q_empty(queue_t *q);
void q_clear(queue_t *q);
void q_destroy(queue_t *q);

#ifdef __cplusplus
}
#endif

#endif
