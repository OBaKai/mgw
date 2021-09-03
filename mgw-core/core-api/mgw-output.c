#include "mgw.h"
#include "util/base.h"
#include "util/tlog.h"
#include "buffer/ring-buffer.h"

#define RTMP_OUTPUT		"rtmp_output"
#define RTSP_OUTPUT		"rtsp_output"
#define HLS_OUTPUT		"hls_output"
#define SRT_OUTPUT		"srt_output"
#define FLV_OUTPUT		"flv_output"
#define FFMPEG_OUTPUT	"ffmpeg_output"

#define MGW_OUTPUT_RETRY_SEC	2
#define MGW_OUTPUT_RETRY_MAX	20

extern struct mgw_core *mgw;

static bool inline mgw_output_active(struct mgw_output *output)
{
    return output && output->context.info_impl &&
            os_atomic_load_bool(&output->actived);
}

static int output_actived(void *opaque, call_params_t *call_params)
{
	return mgw_output_active((mgw_output_t*)opaque);
}

static inline bool reconnecting(const struct mgw_output *output)
{
    return os_atomic_load_bool(&output->reconnecting);
}

static inline bool stopping(const struct mgw_output *output)
{
    return os_event_try(output->stopping_event) == EAGAIN;
}

static inline bool actived(const struct mgw_output *output)
{
	return os_atomic_load_bool(&output->actived);
}

const struct mgw_output_info *find_output_info(const char *id)
{
	size_t i;
	for (i = 0; i < mgw->output_types.num; i++)
		if (strcmp(mgw->output_types.array[i].id, id) == 0)
			return mgw->output_types.array + i;

	return NULL;
}

static proc_handler_t *output_get_source_proc_handler(mgw_output_t *output)
{
	return output ? output->parent_stream->source->context.procs : NULL;
}

static inline int output_proc_handler(mgw_output_t *output,
					const char *name, call_params_t *params)
{
	proc_handler_t *procs = mgw_stream_get_procs(output->parent_stream);
	params->in = output->context.obj_name;
	params->in_size = strlen(params->in) + 1;
	return proc_handler_do(procs, name, params);
}

static bool mgw_output_actual_start(mgw_output_t *output)
{
	bool success = false;
	os_event_wait(output->stopping_event);
	output->stop_code = 0;

	if (output->valid && output->context.info_impl)
		if ((success = output->info.start(output->context.info_impl))) {
			call_params_t params = {};
			output_proc_handler(output, "started", &params);
		}

	os_atomic_set_bool(&output->actived, success);
	return success;
}

static void mgw_output_actual_stop(mgw_output_t *output, bool force)
{
	if (stopping(output) && !force)
		return;

	os_event_reset(output->stopping_event);
	if (reconnecting(output)) {
		os_event_signal(output->reconnect_stop_event);
		if (output->reconnect_thread_active)
			pthread_join(output->reconnect_thread, NULL);
	}

    os_event_signal(output->stopping_event);
	if (force && output->context.info_impl) {
        blog(MGW_LOG_INFO, "will call info.stop  ");
		output->info.stop(output->context.info_impl);
	} else if (reconnecting(output)) {
		output->stop_code = MGW_SUCCESS;
	}
	os_atomic_set_bool(&output->actived, false);
}

static void *reconnect_thread(void *param)
{
	struct mgw_output *output = param;
	unsigned long ms = output->reconnect_retry_cur_sec * 1000;

	output->reconnect_thread_active = true;
	{
		call_params_t params = {};
		output_proc_handler(output, "reconnect", &params);
	}

	if (os_event_timedwait(output->reconnect_stop_event, ms) == ETIMEDOUT)
		mgw_output_actual_start(output);

	if (os_event_try(output->reconnect_stop_event) == EAGAIN)
		pthread_detach(output->reconnect_thread);
	else
		os_atomic_set_bool(&output->reconnecting, false);

	output->reconnect_thread_active = false;
	return NULL;
}

static void *stop_thread(void *arg)
{
	mgw_output_t *output = arg;
	if (!output || stopping(output))
		return NULL;

	tlog(TLOG_INFO, "stop output:%s internal!\n", output->context.info_id);
	{
		call_params_t params = {};
		output_proc_handler(output, "stop", &params);
	}

	return NULL;
}

#define MAX_RETRY_SEC (15 * 60)

static void output_reconnect(struct mgw_output *output)
{
	int ret;

	if (!reconnecting(output)) {
		output->reconnect_retry_cur_sec = output->reconnect_retry_sec;
		output->reconnect_retries = 0;
	}

	if (output->reconnect_retries >= output->reconnect_retry_max) {
		output->stop_code = MGW_DISCONNECTED;
		os_atomic_set_bool(&output->reconnecting, false);
		if (0 == pthread_create(&output->stop_thread, NULL, stop_thread, output)) {
			pthread_detach(output->stop_thread);
		}
		return;
	}

	if (!reconnecting(output)) {
		os_atomic_set_bool(&output->reconnecting, true);
		os_event_reset(output->reconnect_stop_event);
	}

	if (output->reconnect_retries) {
		output->reconnect_retry_cur_sec *= 2;
		if (output->reconnect_retry_cur_sec > MAX_RETRY_SEC)
			output->reconnect_retry_cur_sec = MAX_RETRY_SEC;
	}

	output->reconnect_retries++;

    output->info.stop(output->context.info_impl);

	output->stop_code = MGW_DISCONNECTED;
	ret = pthread_create(&output->reconnect_thread, NULL, &reconnect_thread,
			     output);
	if (ret < 0) {
		tlog(TLOG_WARN, "Failed to create reconnect thread");
		os_atomic_set_bool(&output->reconnecting, false);
	} else {
		tlog(TLOG_INFO, "Output '%s':  Reconnecting in %d seconds..",
							output->context.info_id, output->reconnect_retry_sec);
		const char *name = output->context.obj_name;
		//signal_reconnect(output);
	}
}

/**< First time reconnect must be return MGW_OUTPUT_DISCONNECTED value
 *   and other time do not return MGW_OUTPUT_SUCCESS
 */
static inline bool can_reconnect(const mgw_output_t *output, int code)
{
	bool reconnect_active = output->reconnect_retry_max != 0;

	return (reconnecting(output) && code != MGW_SUCCESS) ||
	       (reconnect_active && code == MGW_DISCONNECTED);
}

static int output_signal_stop(void *opaque, struct call_params *params)
{
	mgw_output_t *output = opaque;
	if (!output || !params || !params->in)
		return;

	output->stop_code = *((int*)params->in);
	if (!output->reconnect_retries && output->stop_code != MGW_DISCONNECTED)
		output->last_error_status = output->stop_code;

	if (can_reconnect(output, output->stop_code)) {
		output_reconnect(output);
	} else {
		if (!reconnecting(output) && actived(output)) {
			if (0 == pthread_create(&output->stop_thread, NULL, stop_thread, output)) {
				pthread_detach(output->stop_thread);
			}
		}
        os_event_signal(output->stopping_event);
		os_atomic_set_bool(&output->actived, false);
        blog(MGW_LOG_INFO, "signal stop output");
	}
}

static int output_source_ready(void *opaque, struct call_params *params)
{
	mgw_output_t *output = opaque;
	return !!output && output->valid && !!output->buffer;
}

static int output_get_encoder_packet(
		mgw_output_t *output, struct encoder_packet *packet)
{
	if (!output || !packet || !output->buffer)
		return FRAME_CONSUME_PERR;

	return mgw_rb_read_packet(output->buffer, packet);
}

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

static bool mgw_output_init(struct mgw_output *output)
{
	output->control = bzalloc(sizeof(struct mgw_ref));
	output->control->data 		= output;
	output->reconnect_retry_sec = MGW_OUTPUT_RETRY_SEC;
	output->reconnect_retry_max = MGW_OUTPUT_RETRY_MAX;

	/** initialize buffer */
	mgw_data_t *buf_settings = NULL;
	if (!mgw_data_has_user_value(output->context.settings, "buffer")) {
		buf_settings = mgw_rb_get_default();
		mgw_data_set_string(buf_settings, "io_mode", "read");
		mgw_data_set_string(buf_settings, "stream_name",
					output->parent_stream->context.obj_name);
		mgw_data_set_string(buf_settings, "user_id", output->context.obj_name);
		mgw_data_set_obj(output->context.settings, "buffer", buf_settings);
	} else {
		buf_settings = mgw_data_get_default_obj(output->context.settings, "buffer");
	}

	if (!(output->buffer = mgw_rb_create(buf_settings, NULL)))
		return false;

	if (buf_settings)
		mgw_data_release(buf_settings);

	/** initlialize callback */
	proc_handler_add(output->context.procs, "actived", 			output_actived);
	proc_handler_add(output->context.procs, "signal_stop",		output_signal_stop);
	proc_handler_add(output->context.procs, "source_ready",		output_source_ready);

	output->get_encoder_packet		= output_get_encoder_packet;
	output->get_source_proc_handler	= output_get_source_proc_handler;

	mgw_context_data_insert(&output->context,
				&output->parent_stream->outputs_mutex,
				&output->parent_stream->outputs_list);

	return true;
}

static mgw_output_t *mgw_output_create_internal(
				mgw_stream_t *stream, const char *info_id,
				const char *output_name, mgw_data_t *settings)
{
	struct mgw_output *output = bzalloc(sizeof(struct mgw_output));
	const struct mgw_output_info *info = find_output_info(info_id);
	output->parent_stream = stream;

	if (!info) {
		blog(MGW_LOG_ERROR, "Output ID: %s not found!", info_id);
		output->info.id			= bstrdup(info_id);
	} else {
		output->info = *info;
	}

	if (0 != os_event_init(&output->stopping_event, OS_EVENT_TYPE_MANUAL))
		goto failed;
	if (0 != os_event_init(&output->reconnect_stop_event, OS_EVENT_TYPE_MANUAL))
		goto failed;

	if (!settings && info && info->get_default)
		settings = info->get_default();

	if (!mgw_context_data_init(output, &output->context,
				MGW_OBJ_TYPE_OUTPUT, settings, output_name, false))
		goto failed;

	if (!mgw_output_init(output))
		goto failed;

	if (info)
		output->context.info_impl =
			info->create(output->context.settings, output);

	output->valid = true;
	blog(MGW_LOG_DEBUG, "Output %s (%s) created!", output_name, info_id);
	return output;

failed:
	bfree(output->control);
	mgw_output_destroy(output);
	return NULL;
}

mgw_output_t *mgw_output_create(struct mgw_stream *stream,
				const char *output_name, mgw_data_t *settings)
{
	const char *info_id = NULL;
	const char *protocol = mgw_data_get_string(settings, "protocol");
	if (!(*protocol)) {
		protocol = mgw_data_get_string(settings, "uri");
		if (!(*protocol)) {
			tlog(TLOG_ERROR, "Couldn't find output protocol!\n");
			return NULL;
		}
	}
	if (*protocol)
		info_id = get_output_id(protocol);

	return mgw_output_create_internal(stream, info_id, output_name, settings);
}

void mgw_output_destroy(struct mgw_output *output)
{
	if (!output) return;

	tlog(TLOG_INFO, "Output %s destroyed!", output->context.obj_name);
	if (output->valid && mgw_output_active(output))
		mgw_output_actual_stop(output, true);

	os_event_wait(output->stopping_event);
	if (output->context.info_impl)
        output->info.destroy(output->context.info_impl);

	/** Destroy buffer */
	if (output->buffer)
		mgw_rb_destroy(output->buffer);

	os_event_destroy(output->stopping_event);
	os_event_destroy(output->reconnect_stop_event);

	/** destroy output resource */
	mgw_context_data_free(&output->context);

	bfree((void*)output->info.id);
	bfree(output);
}

void mgw_output_addref(mgw_output_t *output)
{
	if (output && output->control)
		mgw_ref_addref(output->control);
}

void mgw_output_release(mgw_output_t *output)
{
	if (!output) return;

	struct mgw_ref *ctrl = output->control;
	if (mgw_ref_release(ctrl)) {
		mgw_output_destroy(output);
		bfree(ctrl);
	}
}

mgw_output_t *mgw_output_get_ref(mgw_output_t *output)
{
	if (!output) return NULL;
	return mgw_get_ref(output->control) ?
		(mgw_output_t*)output->control->data : NULL;
}

mgw_output_t *mgw_get_weak_output(mgw_output_t *output)
{
	if (!output) return NULL;
	return (mgw_output_t*)output->control->data;
}

bool mgw_output_references_output(
			struct mgw_ref *ref,mgw_output_t *output)
{
	return ref && output && ref->data == output;
}

const char *mgw_output_get_name(const mgw_output_t *output)
{
	if (!output) return NULL;
	return output->context.obj_name;
}

bool mgw_output_start(mgw_output_t *output)
{
	if (!output || (!output->context.is_private &&
			!output->context.info_impl))
		return false;

	if (!output->buffer) return false;
    os_event_signal(output->stopping_event);
	return mgw_output_actual_start(output);
}

void mgw_output_stop(mgw_output_t *output)
{
	if (!output || !actived(output)) return;
	mgw_output_actual_stop(output, true);
}

mgw_data_t *mgw_output_get_state(mgw_output_t *output)
{
    if (!output) return NULL;

	int push_state = 0;
	if (!output_source_ready(output, NULL))
		push_state = MGW_OUTPUT_STATUS_STOPED;
	else if (mgw_output_active(output))
		push_state = MGW_OUTPUT_STATUS_STREAMING;
	else if (reconnecting(output))
		push_state = MGW_OUTPUT_STATUS_RECONNECTING;
	else if (output_source_ready(output, NULL) &&
			!mgw_output_active(output))
		push_state = MGW_OUTPUT_STATUS_CONNECTING;

	bool authen = false;
	const char *username = mgw_data_get_string(output->context.settings, "username");
	if (username && strlen(username))
		authen = true;

    mgw_data_t *state_info = mgw_data_create();
    mgw_data_set_bool(state_info, "active", mgw_output_active(output));
	mgw_data_set_int(state_info, "state", push_state);
	mgw_data_set_int(state_info, "failed_cnt", os_atomic_load_long(&output->failed_count));
	mgw_data_set_bool(state_info, "authen", authen);

    return state_info;
}