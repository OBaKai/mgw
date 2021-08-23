#ifndef _MGW_CORE_MGW_BASIC_H_
#define _MGW_CORE_MGW_BASIC_H_

#include "util/c99defs.h"
#include "util/mgw-data.h"
#include "util/codec-def.h"
#include "util/darray.h"

#ifdef __cpluplus
extern "C" {
#endif

typedef enum stream_signal {
	STREAM_OUTPUT_STOP = 0,		/** output id will callback, type: char* string */
	STREAM_OUTPUT_RECONNECTING,
	STREAM_OUTPUT_CONNECTED,
}stream_sig_t;

typedef int (*stream_cb)(int cmd, void *param, size_t param_size);

bool mgw_app_startup(const char *config_path);
void mgw_app_exit(void);

/** basic functions */
// void *mgw_stream_create(const char *name, stream_cb cb);
// void mgw_stream_destroy(void *data);

// bool mgw_stream_add_source(void *data, mgw_data_t *settings);
// bool mgw_stream_add_private_source(void *data, mgw_data_t *settings);
// void mgw_stream_release_source(void *data);
bool mgw_stream_has_source(void *data);

// int mgw_stream_add_ouptut(void *data, mgw_data_t *settings);
// bool mgw_stream_add_private_output(void *data, mgw_data_t *settings);
// void mgw_stream_release_output(void *data, const char *id);
bool mgw_stream_has_output(void *data);

// mgw_data_t *mgw_stream_get_info(void *data, const char *id);
// mgw_data_t *mgw_stream_get_output_setting(void *data, const char *id);

// bool mgw_stream_send_private_packet(void *data, struct encoder_packet *packet);

/**< Internal signal status */
// void mgw_stream_signal_stop_output_internal(void *data, char *output_id);
// void mgw_stream_signal_reconnecting_internal(void *data, char *output_id);
// void mgw_stream_signal_connected_internal(void *data, char *output_id);

#ifdef __cpluplus
}
#endif
#endif  //_MGW_CORE_MGW_BASIC_H_