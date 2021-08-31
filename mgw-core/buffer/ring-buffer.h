#ifndef _MGW_CORE_RING_BUFFER_H_
#define _MGW_CORE_RING_BUFFER_H_

#include "util/mgw-data.h"
#include "util/codec-def.h"

#ifdef __cpluscplus
extern "C"{
#endif

void *mgw_rb_create(mgw_data_t *settings, void *source);
void mgw_rb_destroy(void *data);

void mgw_rb_addref(void *data);
void mgw_rb_release(void *data);
mgw_data_t *mgw_rb_get_default(void);

size_t mgw_rb_write_packet(void *data, struct encoder_packet *packet);
int mgw_rb_read_packet(void *data, struct encoder_packet *packet);

#ifdef __cpluscplus
}
#endif
#endif  //_MGW_CORE_RING_BUFFER_H_