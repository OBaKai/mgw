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

struct mgw_stream {
	mgw_source_t				*source;
	DARRAY(struct mgw_output)	outputs;
	mgw_data_t					*settings;
};

bool mgw_app_startup(const char *config_path);
void mgw_app_exit(void);

/** basic functions */
struct mgw_stream *app_stream_create(const char *name);
void app_stream_destroy(struct mgw_stream *data);

bool app_stream_add_source(struct mgw_stream *data, mgw_data_t *settings);
bool app_stream_add_private_source(struct mgw_stream *data, mgw_data_t *settings);
void app_stream_release_source(struct mgw_stream *data);

bool app_stream_add_ouptut(struct mgw_stream *data, mgw_data_t *settings);
bool app_stream_add_private_output(struct mgw_stream *data, mgw_data_t *settings);
void app_stream_release_output(struct mgw_stream *data, const char *id);

mgw_data_t *app_stream_get_info(struct mgw_stream *data);

bool app_stream_send_private_packet(struct mgw_stream *data, struct encoder_packet *packet);

#ifdef __cpluplus
}
#endif
#endif  //_MGW_CORE_MGW_BASIC_H_