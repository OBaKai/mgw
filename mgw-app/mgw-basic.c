#include "mgw-basic.h"
#include "mgw.h"

#include "util/base.h"
#include "util/darray.h"

#include <string.h>

#define RTMP_OUTPUT		"rtmp_output"
#define RTSP_OUTPUT		"rtsp_output"
#define HLS_OUTPUT		"hls_output"


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
struct mgw_stream *app_stream_create(const char *name)
{
	struct mgw_stream *stream = (struct mgw_stream *)bzalloc(sizeof(struct mgw_stream));
	stream->settings = mgw_data_create();
	mgw_data_set_string(stream->settings, "name", name);
	return stream;
}

void app_stream_destroy(struct mgw_stream *data)
{
	struct mgw_stream *stream = data;
	if (!stream)
		return;

	if (stream->source)
		mgw_source_release(stream->source);

	for (int i = 0; i < stream->outputs.num; i++) {
		struct mgw_output *output = &stream->outputs.array[i];
		mgw_output_stop(output);
		mgw_output_release(output);
	}
	da_free(stream->outputs);

	if (stream->settings)
		mgw_data_release(stream->settings);
}

static bool app_stream_add_source_internal(
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
	mgw_source_addref(source);
    blog(LOG_INFO, "[%s][%d]: source(%p)", __FILE__, __LINE__, stream->source);

	if (mgw_data_has_user_value(stream->settings, "source"))
		mgw_data_erase(stream->settings, "source");

	mgw_data_set_obj(stream->settings, "source", settings);
	return true;
}

/** ---------------------------------------------- */
/** Sources */
bool app_stream_add_source(struct mgw_stream *data, mgw_data_t *settings)
{
	struct mgw_stream *stream = (struct mgw_stream *)data;
	if (!stream || !settings)
		return false;
	if (stream->source)
		return true;

	return app_stream_add_source_internal(stream, settings, false);
}

bool app_stream_add_private_source(struct mgw_stream *data, mgw_data_t *settings)
{
	struct mgw_stream *stream = (struct mgw_stream *)data;
	if (!stream || !settings)
		return false;
	return app_stream_add_source_internal(stream, settings, true);
}

void app_stream_release_source(struct mgw_stream *data)
{
	struct mgw_stream *stream = (struct mgw_stream *)data;
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
	else if (!strncasecmp(protocol, "rtsp", 4) ||
			 !strncasecmp(protocol, "rtsps", 5))
		return RTSP_OUTPUT;
	else if (!strncasecmp(protocol, "flv", 3))
		return "flv_output";
	else if (!strncasecmp(protocol, "mp4", 3))
		return "ffmpeg_output";
	else if (!strncasecmp(protocol, "hls", 3))
		return HLS_OUTPUT;
	else
		return NULL;
}

bool app_stream_add_ouptut(struct mgw_stream *data, mgw_data_t *settings)
{
	const char *name = NULL, *id = NULL, *old_id = NULL;
	const char *protocol = NULL;
	struct mgw_output *output = NULL;
	struct mgw_stream *stream = (struct mgw_stream *)data;

	if (!stream || !settings)
		return false;

	name = mgw_data_get_string(stream->settings, "name");
	id = mgw_data_get_string(settings, "id");
	protocol = mgw_data_get_string(settings, "protocol");

	for (int i = 0; i < stream->outputs.num; i++) {
		struct mgw_output *out = stream->outputs.array + i;
		old_id = mgw_data_get_string(out->context.settings, "id");
		if (!strcmp(id, old_id))
			return false;
	}

	output = mgw_output_create(get_output_id(protocol), name, settings);
	if (!output) {
		blog(LOG_ERROR, "Tried to create output failed!");
		return false;
	}

	if (!mgw_output_start(output))
		blog(LOG_INFO, "Tried to start output:%s failed, try again!", id);

	da_push_back(stream->outputs, output);
	mgw_output_addref(output);

	if (mgw_data_has_user_value(stream->settings, "outputs")) {
		mgw_data_array_t *outputs = mgw_data_get_array(stream->settings, "outputs");
		size_t output_cnt = mgw_data_array_count(outputs);
		for (int i = 0; i < output_cnt; i++) {
			mgw_data_t *out_settings = mgw_data_array_item(outputs, i);
			const char *exist_id = mgw_data_get_string(out_settings, "id");
			if (!strcmp(id, exist_id)) {
				mgw_data_array_erase(outputs, i);
				mgw_data_array_insert(outputs, i, settings);
				break;
			}
		}
		mgw_data_array_push_back(outputs, settings);
		mgw_data_array_release(outputs);
	} else {
		mgw_data_array_t *outputs = mgw_data_array_create();
		mgw_data_array_push_back(outputs, settings);
		mgw_data_set_array(stream->settings, "outputs", outputs);
		mgw_data_array_release(outputs);
	}

	return true;
}

bool app_stream_add_private_output(struct mgw_stream *data, mgw_data_t *settings)
{
	return false;
}

void app_stream_release_output(struct mgw_stream *data, const char *id)
{
	struct mgw_stream *stream = (struct mgw_stream *)data;
	if (!stream || !id)
		return;

	for (int i = 0; i < stream->outputs.num; i++) {
		struct mgw_output *output = stream->outputs.array + i;
		const char *exist_id = mgw_data_get_string(output->context.settings, "id");
		if (!strcmp(id, exist_id)) {
			mgw_output_stop(output);
			mgw_output_release(output);
			da_erase_item(stream->outputs, output);

			mgw_data_array_t *outputs = mgw_data_get_array(stream->settings, "outputs");
			size_t output_cnt = mgw_data_array_count(outputs);
			for (int i = 0; i < output_cnt; i++) {
				mgw_data_t *out_settings = mgw_data_array_item(outputs, i);
				const char *exist_id = mgw_data_get_string(out_settings, "id");
				if (!strcmp(id, exist_id)) {
					mgw_data_array_erase(outputs, i);
					break;
				}
			}
			break;
		}
	}
}

/** ---------------------------------------------- */
/** Mis */
mgw_data_t *app_stream_get_info(struct mgw_stream *data)
{
    return NULL;
}

bool app_stream_send_private_packet(struct mgw_stream *data, struct encoder_packet *packet)
{
	struct mgw_stream *stream = (struct mgw_stream *)data;
	if (!stream || !stream->source)
		return false;

	mgw_source_write_packet(stream->source, packet);
	return true;
}