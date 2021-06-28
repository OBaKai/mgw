#ifndef _MGW_CORE_MGW_BASIC_H_
#define _MGW_CORE_MGW_BASIC_H_

#include "util/c99defs.h"
#include "util/mgw-data.h"
#include "util/codec-def.h"
#include "util/darray.h"
#include "mgw-internal.h"

#ifdef __cpluplus
extern "C" {
#endif

bool mgw_app_startup(const char *config_path);
void mgw_app_exit(void);

/** basic functions */
void *mgw_stream_create(const char *name);
void mgw_stream_destroy(void *data);

bool mgw_stream_add_source(void *data, mgw_data_t *settings);
bool mgw_stream_add_private_source(void *data, mgw_data_t *settings);
void mgw_stream_release_source(void *data);

bool mgw_stream_has_source(void *data);

bool mgw_stream_add_ouptut(void *data, mgw_data_t *settings);
bool mgw_stream_add_private_output(void *data, mgw_data_t *settings);
void mgw_stream_release_output(void *data, const char *id);

mgw_data_t *mgw_stream_get_info(void *data);

bool mgw_stream_send_private_packet(void *data, struct encoder_packet *packet);

#ifdef __cpluplus
}
#endif
#endif  //_MGW_CORE_MGW_BASIC_H_