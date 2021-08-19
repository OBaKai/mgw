/**
 * File: mgw-basic.c
 * 
 * Description: Manager source stream and output stream
 * 
 * Datetime: 2021/5/10
 * */
#include "mgw-basic.h"
#include "mgw.h"

#include "util/base.h"
#include "util/tlog.h"
#include "util/darray.h"

#include <string.h>

#define RTMP_OUTPUT		"rtmp_output"
#define RTSP_OUTPUT		"rtsp_output"
#define HLS_OUTPUT		"hls_output"
#define SRT_OUTPUT		"srt_output"
#define FLV_OUTPUT		"flv_output"
#define FFMPEG_OUTPUT	"ffmpeg_output"

struct mgw_stream {
	mgw_source_t				*source;
	DARRAY(struct mgw_output *)	outputs;
	mgw_data_t					*settings;

	stream_cb					cb;
};

/** The configuration should not include source and output, just descrip mgw system settings */
bool mgw_app_startup(const char *config_path)
{
    bool success = false;
	mgw_data_t *mgw_config = mgw_data_create_from_json_file(config_path);
	success = mgw_startup(mgw_config);
    mgw_data_release(mgw_config);
    return success;
}

void mgw_app_exit(void)
{
	mgw_shutdown();
}

/** basic functions */
void *mgw_stream_create(const char *name, stream_cb cb)
{
	struct mgw_stream *stream = bzalloc(sizeof(struct mgw_stream));
	stream->settings = mgw_data_create();
	mgw_data_set_string(stream->settings, "name", name);
	stream->cb = cb;

	return stream;
}

void mgw_stream_destroy(void *data)
{
	struct mgw_stream *stream = data;
	if (!stream)
		return;

	if (stream->source)
		mgw_source_release(stream->source);

	for (int i = 0; i < stream->outputs.num; i++) {
		struct mgw_output **output = &stream->outputs.array[i];
		mgw_output_stop((*output));
		mgw_output_release((*output));
		da_erase_item(stream->outputs, output);
	}

    if (stream->outputs.num > 0)
	    da_free(stream->outputs);

	if (stream->settings)
		mgw_data_release(stream->settings);
}

bool mgw_stream_has_source(void *data)
{
	struct mgw_stream *stream = data;
	if (!stream)
		return false;

	return !!stream->source;
}

static bool mgw_stream_add_source_internal(
		struct mgw_stream *stream, mgw_data_t *settings, bool is_private)
{
	mgw_source_t *source = NULL;
	const char *name = NULL, *source_id = NULL;

	name = mgw_data_get_string(stream->settings, "name");
	source_id = mgw_data_get_string(settings, "id");

	if (is_private)
		source = mgw_source_create_private(source_id, name, settings);
	else
		source = mgw_source_create(source_id, name, settings);
	if (!source)
		return false;

	stream->source = source;

	if (mgw_data_has_user_value(stream->settings, "source"))
		mgw_data_erase(stream->settings, "source");

	mgw_data_set_obj(stream->settings, "source", settings);
	return true;
}

/** ---------------------------------------------- */
/** Sources */
bool mgw_stream_add_source(void *data, mgw_data_t *settings)
{
	struct mgw_stream *stream = data;
	if (!stream || !settings)
		return false;
	if (stream->source)
		return true;

	return mgw_stream_add_source_internal(stream, settings, false);
}

bool mgw_stream_add_private_source(void *data, mgw_data_t *settings)
{
	struct mgw_stream *stream = data;
	if (!stream || !settings)
		return false;
	return mgw_stream_add_source_internal(stream, settings, true);
}

void mgw_stream_release_source(void *data)
{
	struct mgw_stream *stream = data;
	if (!stream)
		return;

	mgw_source_release(stream->source);
	mgw_data_erase(stream->settings, "source");
	stream->source = NULL;
}

/** ---------------------------------------------- */
/** Outputs */

static const char *get_output_id(const char *protocol)
{
	if (!strncasecmp(protocol, "rtmp", 4) ||
		!strncasecmp(protocol, "rtmpt", 5) ||
		!strncasecmp(protocol, "rtmps", 5))
		return RTMP_OUTPUT;
	else if (!strncasecmp(protocol, "srt", 3))
		return SRT_OUTPUT;
	else if (!strncasecmp(protocol, "rtsp", 4) ||
			 !strncasecmp(protocol, "rtsps", 5))
		return RTSP_OUTPUT;
	else if (!strncasecmp(protocol, "flv", 3))
		return FLV_OUTPUT;
	else if (!strncasecmp(protocol, "mp4", 3))
		return FFMPEG_OUTPUT;
	else if (!strncasecmp(protocol, "hls", 3))
		return HLS_OUTPUT;
	else
		return NULL;
}

int mgw_stream_add_ouptut(void *data, mgw_data_t *settings)
{
	const char *name = NULL, *id = NULL, *old_id = NULL;
	const char *protocol = NULL;
	struct mgw_output *output = NULL;
	struct mgw_stream *stream = data;
	bool success = false;

	if (!stream || !settings)
		return -1;

	/**< Name is the stream name */
	name = mgw_data_get_string(stream->settings, "name");
	/**< Unique id to recognize the output channel */
	id = mgw_data_get_string(settings, "id");
	protocol = mgw_data_get_string(settings, "protocol");

	for (int i = 0; i < stream->outputs.num; i++) {
		struct mgw_output **out = stream->outputs.array + i;
		old_id = mgw_data_get_string((*out)->context.settings, "id");
		if (!strcmp(id, old_id))
			return -1;
	}

	output = mgw_output_create(get_output_id(protocol), name, settings, stream);
	if (!output) {
		blog(MGW_LOG_ERROR, "Tried to create output failed!");
		return -1;
	}

	if (!(success = mgw_output_start(output))) {
		int result = output->last_error_status;
		tlog(TLOG_INFO, "Tried to start output:%s failed, code:%d, try again!", id, result);
		mgw_output_destroy(output);
		return result;
	}

	da_push_back(stream->outputs, &output);
	blog(MGW_LOG_INFO, "add output success, outputs.num:%ld", stream->outputs.num);

	if (mgw_data_has_user_value(stream->settings, "outputs")) {
		mgw_data_array_t *outputs = mgw_data_get_array(stream->settings, "outputs");
		size_t output_cnt = mgw_data_array_count(outputs);
		for (int i = 0; i < output_cnt; i++) {
			mgw_data_t *out_settings = mgw_data_array_item(outputs, i);
			const char *exist_id = mgw_data_get_string(out_settings, "id");
			if (!strcmp(id, exist_id)) {
				mgw_data_array_erase(outputs, i);
				mgw_data_array_insert(outputs, i, settings);
				mgw_data_release(out_settings);
				break;
			}
			mgw_data_release(out_settings);
		}
		mgw_data_array_push_back(outputs, settings);
		mgw_data_array_release(outputs);
	} else {
		mgw_data_array_t *outputs = mgw_data_array_create();
		mgw_data_array_push_back(outputs, settings);
		mgw_data_set_array(stream->settings, "outputs", outputs);
		mgw_data_array_release(outputs);
	}

	return 0;
}

bool mgw_stream_add_private_output(void *data, mgw_data_t *settings)
{
	return false;
}

void mgw_stream_release_output(void *data, const char *id)
{
	struct mgw_stream *stream = data;
	if (!stream || !id)
		return;

	for (int i = 0; i < stream->outputs.num; i++) {
		struct mgw_output **output = stream->outputs.array + i;
		const char *exist_id = mgw_data_get_string((*output)->context.settings, "id");
		if (!strcmp(id, exist_id)) {
			mgw_output_stop((*output));
			mgw_output_release((*output));
			da_erase_item(stream->outputs, output);
			tlog(TLOG_INFO, "stop and release: %s success, output.num:%ld",
							exist_id, stream->outputs.num);

			mgw_data_array_t *outputs = mgw_data_get_array(stream->settings, "outputs");
			size_t output_cnt = mgw_data_array_count(outputs);
			for (int i = 0; i < output_cnt; i++) {
				mgw_data_t *out_settings = mgw_data_array_item(outputs, i);
				const char *exist_id = mgw_data_get_string(out_settings, "id");
				if (!strcmp(id, exist_id)) {
					mgw_data_array_erase(outputs, i);
					mgw_data_release(out_settings);
					blog(MGW_LOG_INFO, "erase output:%s", id);
					break;
				}
				mgw_data_release(out_settings);
			}
			mgw_data_array_release(outputs);
			break;
		}
	}
}

bool mgw_stream_has_output(void *data)
{
	struct mgw_stream *stream = data;
	if (!stream)
		return NULL;

	return stream->outputs.num > 0;
}

static inline void signal_output_status(struct mgw_stream *stream, char *id, int status)
{
	stream->cb(status, id, strlen(id));
}

void mgw_stream_signal_stop_output_internal(void *data, char *output_id)
{
	if (data && output_id)
		signal_output_status((struct mgw_stream*)data, output_id, STREAM_OUTPUT_STOP);
}

void mgw_stream_signal_reconnecting_internal(void *data, char *output_id)
{
	if (data && output_id)
		signal_output_status((struct mgw_stream*)data, output_id, STREAM_OUTPUT_RECONNECTING);
}

void mgw_stream_signal_connected_internal(void *data, char *output_id)
{
	if (data && output_id)
		signal_output_status((struct mgw_stream*)data, output_id, STREAM_OUTPUT_CONNECTED);
}

/** ---------------------------------------------- */
/** Mis */
mgw_data_t *mgw_stream_get_info(void *data, const char *output_id)
{
	struct mgw_stream *stream = data;
	if (!stream)
		return NULL;

	mgw_data_t *output_settings = NULL;
	for (int i = 0; i < stream->outputs.num; i++) {
		struct mgw_output **output = stream->outputs.array + i;
		const char *exist_id = mgw_data_get_string((*output)->context.settings, "id");
		if (!strcmp(exist_id, output_id)) {
			output_settings = mgw_output_get_state((*output));
			break;
		}
	}
	return output_settings;
}

mgw_data_t *mgw_stream_get_output_setting(void *data, const char *id)
{
	struct mgw_stream *stream = data;
	if (!data || !id)
		return NULL;

	mgw_data_t *output_settings = NULL;
	for (int i = 0; i < stream->outputs.num; i++) {
		struct mgw_output **output = stream->outputs.array + i;
		const char *exist_id = mgw_data_get_string((*output)->context.settings, "id");
		if (!strcmp(exist_id, id)) {
			output_settings = mgw_data_newref((*output)->context.settings);
			break;
		}
	}
	return output_settings;
}

bool mgw_stream_send_private_packet(void *data, struct encoder_packet *packet)
{
	struct mgw_stream *stream = data;
	if (!stream || !stream->source)
		return false;

	mgw_source_write_packet(stream->source, packet);
	return true;
}