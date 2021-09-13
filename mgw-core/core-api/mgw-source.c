#include "mgw.h"
#include "util/base.h"
#include "util/tlog.h"
#include "util/threading.h"
#include "buffer/ring-buffer.h"

#define FFMPEG_SOURCE	"ffmpeg_source"
#define LOCAL_SOURCE	"local_source"
#define PRIVATE_SOURCE	"private_source"

#define MGW_SOURCE_RETRY_SEC	2
#define MGW_SOURCE_RETRY_MAX	20

extern struct mgw_core *mgw;

static inline bool mgw_source_active(struct mgw_source *source)
{
	return source && source->buffer &&
			os_atomic_load_bool(&source->actived);
}

static inline bool reconnecting(const struct mgw_source *source)
{
    return os_atomic_load_bool(&source->reconnecting);
}

static inline bool stopping(const struct mgw_source *source)
{
    return os_event_try(source->stopping_event) == EAGAIN;
}

bool mgw_source_is_private(mgw_source_t *source)
{
	return source ? (source->is_private && !source->context.info_impl) : false;
}

struct mgw_source_info *get_source_info(const char *id)
{
	for (size_t i = 0; i < mgw->source_types.num; i++) {
		struct mgw_source_info *info = &mgw->source_types.array[i];
		if (strcmp(info->id, id) == 0)
			return info;
	}
	return NULL;
}

static int get_encoder_setting(void *source, struct call_params *params)
{
	mgw_source_t *s = source;
	mgw_data_t *out = NULL;
	if (!source || !params) return MGW_ERR_EPARAM;

	if (s->context.info_impl && !s->is_private)
		out = s->info.get_settings(s->context.info_impl);
	else
		out = mgw_data_get_obj(s->context.settings, "meta");

	params->out = out;
	return MGW_ERR_SUCCESS;
}

static inline void get_header_internal(mgw_source_t *source,
			call_params_t *params, enum encoder_type type)
{
	if (source->context.info_impl && !source->is_private) {
		params->out_size = source->info.get_extra_data(
						source->context.info_impl, type, (uint8_t**)&params->out);

	} else if (source->is_private) {
		if (ENCODER_VIDEO == type) {
			params->out = (uint8_t *)source->video_header.array;
			params->out_size = source->video_header.len;
		} else {
			params->out = (uint8_t *)source->audio_header.array;
			params->out_size = source->audio_header.len;
		}
	}
}

static int get_video_header(void *source, call_params_t *params)
{
	if (!source || !params) return MGW_ERR_EPARAM;
	get_header_internal((mgw_source_t *)source, params, ENCODER_VIDEO);
	return MGW_ERR_SUCCESS;
}

static int get_audio_header(void *source, call_params_t *params)
{
	if (!source || !params) return MGW_ERR_EPARAM;
	get_header_internal((mgw_source_t *)source, params, ENCODER_AUDIO);
	return MGW_ERR_SUCCESS;
}

static inline int source_proc_handler(mgw_source_t *source,
					const char *func, call_params_t *params)
{
	proc_handler_t *procs = mgw_stream_get_procs(source->parent_stream);
	params->in = bstrdup(source->context.obj_name);
	params->in_size = strlen(params->in);
	int ret = proc_handler_do(procs, func, params);
	bfree(params->in);
	return ret;
}

static int signal_started(void *source, call_params_t *params)
{
	return source_proc_handler((mgw_source_t*)source, "signal_started", params);
}

static void *reconnect_thread(void *param)
{
	struct mgw_source *source = param;
	unsigned long ms = source->reconnect_retry_cur_sec * 1000;

	source->reconnect_thread_active = true;
	{
		call_params_t params = {};
		source_proc_handler(source, "signal_reconnect", &params);
	}

	if (os_event_timedwait(source->reconnect_stop_event, ms) == ETIMEDOUT)
		mgw_source_start(source);

	if (os_event_try(source->reconnect_stop_event) == EAGAIN)
		pthread_detach(source->reconnect_thread);
	else
		os_atomic_set_bool(&source->reconnecting, false);

	source->reconnect_thread_active = false;
	return NULL;
}

static void *stop_thread(void *arg)
{
	mgw_source_t *source = arg;
	if (!source || stopping(source))
		return NULL;

	tlog(TLOG_INFO, "stop source:%s internal!\n", source->context.info_id);
	{
		call_params_t params = {};
		source_proc_handler(source, "signal_stop", &params);
	}

	return NULL;
}

#define MAX_RETRY_SEC (15 * 60)
static void source_reconnect(struct mgw_source *source)
{
	int ret;
	if (!reconnecting(source)) {
		source->reconnect_retry_cur_sec = source->reconnect_retry_sec;
		source->reconnect_retries = 0;
	}

	if (source->reconnect_retries >= source->reconnect_retry_max) {
		source->stop_code = MGW_DISCONNECTED;
		os_atomic_set_bool(&source->reconnecting, false);
		if (0 == pthread_create(&source->stop_thread, NULL, stop_thread, source)) {
			pthread_detach(source->stop_thread);
		}
		return;
	}

	if (!reconnecting(source)) {
		os_atomic_set_bool(&source->reconnecting, true);
		os_event_reset(source->reconnect_stop_event);
	}

	if (source->reconnect_retries) {
		source->reconnect_retry_cur_sec *= 2;
		if (source->reconnect_retry_cur_sec > MAX_RETRY_SEC)
			source->reconnect_retry_cur_sec = MAX_RETRY_SEC;
	}

	source->reconnect_retries++;
    source->info.stop(source->context.info_impl);

	source->stop_code = MGW_DISCONNECTED;
	ret = pthread_create(&source->reconnect_thread, NULL, &reconnect_thread,
			     source);
	if (ret < 0) {
		tlog(TLOG_WARN, "Failed to create reconnect thread");
		os_atomic_set_bool(&source->reconnecting, false);
	} else {
		tlog(TLOG_INFO, "source '%s':  Reconnecting in %d seconds..",
							source->context.obj_name, source->reconnect_retry_sec);
		// const char *name = source->context.obj_name;
		//signal_reconnect(source);
	}
}

/**< First time reconnect must be return MGW_OUTPUT_DISCONNECTED value
 *   and other time do not return MGW_OUTPUT_SUCCESS
 */
static inline bool can_reconnect(mgw_source_t *source, int code)
{
	bool reconnect_active = source->reconnect_retry_max != 0;

	return (reconnecting(source) && code != MGW_SUCCESS) ||
	       (reconnect_active && code == MGW_DISCONNECTED);
}

static int signal_stop(void *opaque, call_params_t *params)
{
	mgw_source_t *source = opaque;
	if (!source || !params || !params->in)
		return mgw_out_err(MGW_ERR_EPARAM);

	source->stop_code = *((int*)params->in);
	if (!source->reconnect_retries && source->stop_code != MGW_DISCONNECTED)
		source->last_error_status = source->stop_code;

	if (can_reconnect(source, source->stop_code)) {
		source_reconnect(source);
	} else {
		if (!reconnecting(source) && mgw_source_active(source)) {
			if (0 == pthread_create(&source->stop_thread, NULL, stop_thread, source)) {
				pthread_detach(source->stop_thread);
			}
		}
        os_event_signal(source->stopping_event);
		os_atomic_set_bool(&source->actived, false);
        blog(MGW_LOG_INFO, "signal stop source");
	}

	return 0;
}

bool mgw_source_init_context(struct mgw_source *source,
		mgw_data_t *settings, const char *source_name, bool is_private)
{
	if (!mgw_context_data_init(source, &source->context,
			MGW_OBJ_TYPE_SOURCE, settings, source_name, is_private))
		return false;
	/** signal notify handlers register */
	proc_handler_add(source->context.procs, "get_video_header", get_video_header);
	proc_handler_add(source->context.procs, "get_audio_header", get_audio_header);
	proc_handler_add(source->context.procs, "get_encoder_settings", get_encoder_setting);
	proc_handler_add(source->context.procs, "signal_started", signal_started);
	proc_handler_add(source->context.procs, "signal_stop", signal_stop);

	return true;
}

void mgw_source_set_video_extra_data(mgw_source_t *source,
		uint8_t *extra_data, size_t size)
{
	if (!source) return;
	if (source->context.info_impl && !source->is_private)
		return;

	if (source->is_private)
		bmem_copy(&source->video_header, (const char*)extra_data, size);
}

void mgw_source_set_audio_extra_data(mgw_source_t *source,
		uint8_t channels, uint8_t samplesize, uint32_t samplerate)
{
	uint8_t *header = NULL;

	if (!source) return;
	if (source->context.info_impl && !source->is_private)
		return;

	if (source->is_private) {
		size_t header_size = mgw_get_aaclc_flv_header(
							channels, samplesize, samplerate, &header);
		bmem_copy(&source->audio_header, (const char *)header, header_size);
        bfree(header);
	}
}

static bool mgw_source_init(struct mgw_source *source)
{
	source->control = bzalloc(sizeof(struct mgw_ref));
	source->control->data = source;
	source->reconnect_retry_sec = MGW_SOURCE_RETRY_SEC;
	source->reconnect_retry_max = MGW_SOURCE_RETRY_MAX;
	
	if (!source->enabled) {
		mgw_data_t *buf_settings = mgw_rb_get_default();
		if (buf_settings) {
			mgw_data_set_string(buf_settings, "io_mode", "write");
			mgw_data_set_string(buf_settings, "stream_name",
						source->parent_stream->context.obj_name);
			mgw_data_set_string(buf_settings, "user_id", source->context.obj_name);
		}

		if (!(source->buffer = mgw_rb_create(buf_settings, source))) {
			mgw_data_release(buf_settings);
			return false;
		}

		mgw_data_set_obj(source->context.settings, "buffer", buf_settings);
        mgw_data_release(buf_settings);
	}

	source->active			= mgw_source_active;
	source->output_packet	= mgw_source_write_packet;
	source->update_settings	= mgw_source_update_settings;

	if (source->is_private) {
		mgw_data_t *meta = mgw_data_get_obj(source->context.settings, "meta");
		if (meta) {
			const char *video_payload = mgw_data_get_string(meta, "vencoderID");
			uint32_t channels = mgw_data_get_int(meta, "channels");
			uint32_t samplerate = mgw_data_get_int(meta, "samplerate");
			uint32_t samplesize = mgw_data_get_int(meta, "samplesize");

			mgw_source_set_audio_extra_data(source, channels, samplesize, samplerate);
			if (!strcmp(video_payload, "avc1"))
				source->video_payload = ENCID_H264;
		}
	}

	return true;
}

static mgw_source_t *mgw_source_create_internal(
				struct mgw_stream *stream, const char *info_id,
                const char *source_name, mgw_data_t *settings)
{
	struct mgw_source *source = bzalloc(sizeof(struct mgw_source));
	const struct mgw_source_info *info = get_source_info(info_id);
	bool is_private = !strncmp(PRIVATE_SOURCE, info_id, strlen(PRIVATE_SOURCE));
	source->parent_stream = stream;

	if (0 != os_event_init(&source->stopping_event, OS_EVENT_TYPE_MANUAL))
		goto failed;
	if (0 != os_event_init(&source->reconnect_stop_event, OS_EVENT_TYPE_MANUAL))
		goto failed;

	if (!info) {
		blog(MGW_LOG_INFO, "Source ID:%s not found! is private source: %d", info_id, is_private);
		if (is_private) {
			source->info.id		= bstrdup(info_id);
			source->is_private	= true;
		}
	} else {
		source->info		= *info;
		source->is_private	= false;
	}

	if (!settings && info)
		settings = info->get_defaults();

	if (!mgw_source_init_context(source, settings, source_name, is_private))
		goto failed;

	if (!mgw_source_init(source))
		goto failed;

	if (info) {
		source->context.info_impl = info->create(source->context.settings, source);
		if (!source->context.info_impl) {
			blog(MGW_LOG_ERROR, "Failed to create source info: %s", info_id);
			goto failed;
		}

		/** Get source settings */
		/** meta, video extra data, audio extra data */
		mgw_data_t *meta_settings = source->info.get_settings(source->context.info_impl);
		if (mgw_data_has_user_value(source->context.settings, "meta"))
			mgw_data_erase(source->context.settings, "meta");

		mgw_data_set_obj(source->context.settings, "meta", meta_settings);
		mgw_data_release(meta_settings);

		uint8_t *header = NULL;
		size_t size = source->info.get_extra_data(source->context.info_impl, ENCODER_VIDEO, &header);
		bmem_copy(&source->video_header, (const char *)header, size);
		bfree(header);

		size = source->info.get_extra_data(source->context.info_impl, ENCODER_AUDIO, &header);
		bmem_copy(&source->audio_header, (const char *)header, size);
		bfree(header);
	}

	// mgw_data_t *meta = mgw_data_get_obj(source->context.settings, "meta");
	// if (meta) {
	// 	const char *video_payload = mgw_data_get_string(meta, "vencoderID");
	// 	uint32_t channels = mgw_data_get_int(meta, "channels");
	// 	uint32_t samplerate = mgw_data_get_int(meta, "samplerate");
	// 	uint32_t samplesize = mgw_data_get_int(meta, "samplesize");
	// 	tlog(TLOG_DEBUG, "setting audio meta chn(%d), samplerate(%d), samplesize(%d)", channels, samplerate, samplesize);
	// 	mgw_source_set_audio_extra_data(source, channels, samplesize, samplerate);
	// 	if (!strcmp(video_payload, "avc1"))
	// 		source->video_payload = ENCID_H264;
	// }

	os_atomic_set_bool(&source->enabled, true);
	return source;

failed:
	tlog(TLOG_ERROR, "Create source %s failed!", source_name);
	bfree(source->control);
	mgw_source_destroy(source);
	return NULL;
}

static const char *get_source_id(const char *protocol)
{
	if (!strncasecmp(protocol, "rtmp", 4) ||
		!strncasecmp(protocol, "rtmpt", 5) ||
		!strncasecmp(protocol, "rtmps", 5) ||
		!strncasecmp(protocol, "srt", 3) ||
		!strncasecmp(protocol, "rtsp", 4) ||
		!strncasecmp(protocol, "rtsps", 5) ||
		!strncasecmp(protocol, "flv", 3) ||
		!strncasecmp(protocol, "mp4", 3) ||
		!strncasecmp(protocol, "hls", 3) ||
		!strncasecmp(protocol, "local", 5))
		return FFMPEG_SOURCE;
	else
		return PRIVATE_SOURCE;
}

mgw_source_t *mgw_source_create(struct mgw_stream *stream,
			const char *source_name, mgw_data_t *settings)
{
	const char *info_id = NULL;
	const char *protocol = mgw_data_get_string(settings, "protocol");
	if (!(*protocol)) {
		protocol = mgw_data_get_string(settings, "uri");
		if (!(*protocol))
			info_id = PRIVATE_SOURCE;
	}
	if (*protocol)
		info_id = get_source_id(protocol);

	return mgw_source_create_internal(stream, info_id, source_name, settings);
}

void mgw_source_destroy(struct mgw_source *source)
{
	if (!source)
		return;

	tlog(TLOG_DEBUG, "%s source %s destroyed!",\
			source->context.is_private ? "private" : "",\
			source->context.obj_name);

	if (source->enabled && mgw_source_active(source))
		mgw_source_stop(source);
	
	if (source->context.info_impl) {
		source->info.destroy(source->context.info_impl);
		source->context.info_impl = NULL;
	}
	mgw_context_data_free(&source->context);

	if (source->buffer)
		mgw_rb_destroy(source->buffer);

	if (source->control)
		bfree(source->control);

	bmem_free(&source->audio_header);
	bmem_free(&source->video_header);

	if (source->is_private && source->info.id)
		bfree((void*)source->info.id);

	bfree(source);
}

void mgw_source_addref(mgw_source_t *source)
{
	if (!source) return;
	mgw_ref_addref(source->control);
}

void mgw_source_release(mgw_source_t *source)
{
	if (!source) return;
	struct mgw_ref *ctrl = source->control;
	if (mgw_ref_release(ctrl)) {
		mgw_source_destroy(source);
		bfree(ctrl);
	}
}

mgw_source_t *mgw_source_get_ref(mgw_source_t *source)
{
	if (!source) return NULL;
	return mgw_get_ref(source->control) ?
		(mgw_source_t*)source->control->data : NULL;
}

mgw_source_t *mgw_get_weak_source(mgw_source_t *source)
{
	if (!source) return NULL;
	return (mgw_source_t*)source->control->data;
}

bool mgw_source_references_source(struct mgw_ref *ref,
		mgw_source_t *source)
{
	return ref && source && ref->data == source;
}

void mgw_source_write_packet(mgw_source_t *source, struct encoder_packet *packet)
{
	uint8_t *header = NULL;

    if (!source || !packet || !source->buffer)
		return;

	if (source->is_private && !source->context.info_impl) {
		int8_t start_code = mgw_avc_get_startcode_len((const uint8_t*)packet->data);
		if (start_code > 0) {
			if (ENCODER_VIDEO == packet->type) {
				if (packet->keyframe && ENCID_H264 == source->video_payload) {
					size_t size = mgw_parse_avc_header(&header, packet->data, packet->size);
					if (size > 4 && header) {
						mgw_source_set_video_extra_data(source, header, size);
						packet->priority = FRAME_PRIORITY_LOW;
						bfree(header);
					}
				}
			}
		}
	}
	mgw_rb_write_packet(source->buffer, packet);
}

void mgw_source_update_settings(mgw_source_t *source, mgw_data_t *settings)
{
	if (!source || !settings)
		return;
}

/** --------------------------------------------------------------------- */
/** For local file source and netstream  */
bool mgw_source_start(struct mgw_source *source)
{
	if (!source || !source->enabled || !source->buffer)
		return false;
	if (mgw_source_is_private(source)) {
		tlog(TLOG_DEBUG, "Source %s is private\n", source->context.obj_name);
		return false;
	}

	if (mgw_source_active(source)) {
		mgw_source_stop(source);
		os_event_wait(source->stopping_event);
		source->stop_code = 0;
		os_atomic_set_bool(&source->actived, false);
	}

	os_atomic_set_bool(&source->actived, true);
	if (reconnecting(source))
		os_event_wait(source->stopping_event);

	if (source->context.info_impl) {
		if ((source->actived = source->info.start(source->context.info_impl))) {
			call_params_t param = {};
			source_proc_handler(source, "signal_connecting", &param);
		}
	}

	return mgw_source_active(source);
}

void mgw_source_stop(struct mgw_source *source)
{
	if (!source || !source->enabled || !source->buffer)
		return;

	if (!source->is_private && !source->context.info_impl)
		return;

	if (source->actived && source->context.info_impl){
		source->info.stop(source->context.info_impl);
		os_event_signal(source->stopping_event);
		source->stop_code = 0;
	}
	source->actived = false;
}
