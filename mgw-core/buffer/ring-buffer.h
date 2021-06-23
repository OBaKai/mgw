#ifndef _MGW_CORE_RING_BUFFER_H_
#define _MGW_CORE_RING_BUFFER_H_

#include "util/mgw-data.h"
#include "util/codec-def.h"

#ifdef __cpluscplus
extern "C"{
#endif

struct source_param {
    void        *source;
    mgw_data_t  *(*source_settings)(void *source);
    size_t      (*audio_header)(void *source, uint8_t **header);
    size_t      (*video_header)(void *source, uint8_t **header);
};

void *mgw_rb_create(mgw_data_t *settings, struct source_param *param);
void mgw_rb_destroy(void *data);

void mgw_rb_addref(void *data);
void mgw_rb_release(void *data);
mgw_data_t *mgw_rb_get_default(void);

struct source_param *mgw_rb_get_source_param(void *data);

size_t mgw_rb_write_packet(void *data, struct encoder_packet *packet);
size_t mgw_rb_read_packet(void *data, struct encoder_packet *packet);

mgw_data_t *mgw_rb_get_encoder_settings(void *data);
size_t mgw_rb_get_video_header(void *data, uint8_t **header);
size_t mgw_rb_get_audio_header(void *data, uint8_t **header);


#ifdef __cpluscplus
}
#endif
#endif  //_MGW_CORE_RING_BUFFER_H_