#ifndef _PLUGINS_THIRDPARTY_MGW_LIBSRT_H_
#define _PLUGINS_THIRDPARTY_MGW_LIBSRT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "util/c99defs.h"

#define SRT_IO_FLAG_READ	1
#define SRT_IO_FLAG_WRITE	2

typedef struct libsrt_interrupt_cb {
	bool (*callback)(void*);
	void *opaque;
}srt_int_cb;

enum srt_mode {
    SRT_MODE_CALLER = 0,
    SRT_MODE_LISTENER = 1,
    SRT_MODE_RENDEZVOUS = 2
};

void *mgw_libsrt_create(srt_int_cb *int_cb, const char *filename, enum srt_mode mode);
int mgw_libsrt_open(void *priv_data, const char *uri, int flags);
int mgw_libsrt_close(void *priv_data);
void mgw_libsrt_destroy(void *priv_data);

int mgw_libsrt_write(void *priv_data, const uint8_t *buf, int size);
int mgw_libsrt_read(void *priv_data, uint8_t *buf, int size);

int mgw_libsrt_get_file_handle(void *priv_data);

int mgw_libsrt_get_payload_size(void *priv_data);
void mgw_libsrt_set_payload_size(void *priv_data, int size);

int64_t mgw_libsrt_get_maxbw(void *priv_data);
void mgw_libsrt_set_maxbw(void *priv_data, int64_t size);

#ifdef __cplusplus
}
#endif
#endif