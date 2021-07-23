/**
 * File: mgw-output.c
 * 
 * Description: The logic implement of mgw output module
 * 
 * Datetime: 2021/5/10
 * */
#include "mgw.h"

#include "util/base.h"
#include "buffer/ring-buffer.h"

#define MGW_OUTPUT_RETRY_SEC	2
#define MGW_OUTPUT_RETRY_MAX	20


extern struct mgw_core *mgw;

static bool mgw_output_active(struct mgw_output *output)
{
    return output && output->context.data &&
            os_atomic_load_bool(&output->actived);
}

static inline bool reconnecting(const struct mgw_output *output)
{
    return os_atomic_load_bool(&output->reconnecting);
}

static inline bool stopping(const struct mgw_output *output)
{
    return os_event_try(output->stopping_event) == EAGAIN;
}

const struct mgw_output_info *find_output(const char *id)
{
	size_t i;
	for (i = 0; i < mgw->output_types.num; i++)
		if (strcmp(mgw->output_types.array[i].id, id) == 0)
			return mgw->output_types.array + i;

	return NULL;
}

static bool mgw_output_actual_start(mgw_output_t *output)
{
	bool success = false;
	os_event_wait(output->stopping_event);
	output->stop_code = 0;

	if (output->valid && output->context.data)
		success = output->info.start(output->context.data);

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
	if (force && output->context.data) {
        blog(MGW_LOG_INFO, "------------>>> will call info.stop  ");
		output->info.stop(output->context.data);
	} else if (reconnecting(output)) {
		output->stop_code = MGW_OUTPUT_SUCCESS;
	}
	os_atomic_set_bool(&output->actived, false);
}

static void *reconnect_thread(void *param)
{
	struct mgw_output *output = param;
	unsigned long ms = output->reconnect_retry_cur_sec * 1000;

	output->reconnect_thread_active = true;

	if (os_event_timedwait(output->reconnect_stop_event, ms) == ETIMEDOUT)
		mgw_output_actual_start(output);

	if (os_event_try(output->reconnect_stop_event) == EAGAIN)
		pthread_detach(output->reconnect_thread);
	else
		os_atomic_set_bool(&output->reconnecting, false);

	output->reconnect_thread_active = false;
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
		output->stop_code = MGW_OUTPUT_DISCONNECTED;
		os_atomic_set_bool(&output->reconnecting, false);
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

    output->info.stop(output->context.data);

	output->stop_code = MGW_OUTPUT_DISCONNECTED;
	ret = pthread_create(&output->reconnect_thread, NULL, &reconnect_thread,
			     output);
	if (ret < 0) {
		blog(MGW_LOG_WARNING, "Failed to create reconnect thread");
		os_atomic_set_bool(&output->reconnecting, false);
	} else {
		blog(MGW_LOG_INFO, "Output '%s':  Reconnecting in %d seconds..",
		     output->context.name, output->reconnect_retry_sec);

		//signal_reconnect(output);
	}
}

static inline bool can_reconnect(const mgw_output_t *output, int code)
{
	bool reconnect_active = output->reconnect_retry_max != 0;

	return (reconnecting(output) && code != MGW_OUTPUT_SUCCESS) ||
	       (reconnect_active && code == MGW_OUTPUT_DISCONNECTED);
}

static void mgw_output_signal_stop(mgw_output_t *output, int code)
{
	if (!output)
		return;

	output->stop_code = code;
	if (can_reconnect(output, code)) {
		output_reconnect(output);
	} else {
		os_atomic_set_bool(&output->actived, false);
        os_event_signal(output->stopping_event);
        blog(MGW_LOG_INFO, "------------------>> signal stop output");
	}
}

static bool mgw_output_source_is_ready(mgw_output_t *output)
{
	return !!output && output->valid && !!output->buffer;
}

static mgw_data_t *mgw_output_get_encoder_settings(mgw_output_t *output)
{
	if (!output || !output->buffer)
		return NULL;

	return mgw_rb_get_encoder_settings(output->buffer);
}

static size_t mgw_output_get_video_header(
		mgw_output_t *output, uint8_t **header)
{
	if (!output || !output->buffer)
		return 0;

	return mgw_rb_get_video_header(output->buffer, header);
}

static size_t mgw_output_get_audio_header(
		mgw_output_t *output, uint8_t **header)
{
	if (!output || !output->buffer)
		return 0;

	return mgw_rb_get_audio_header(output->buffer, header);
}

static int mgw_output_get_next_encoder_packet(
		mgw_output_t *output, struct encoder_packet *packet)
{
	if (!output || !packet || !output->buffer)
		return FRAME_CONSUME_PERR;

	return mgw_rb_read_packet(output->buffer, packet);
}

mgw_output_t *mgw_output_create(const char *id,
                const char *name, mgw_data_t *settings)
{
	struct mgw_output *output = bzalloc(sizeof(struct mgw_output));
	const struct mgw_output_info *info = find_output(id);

	if (!info) {
		blog(MGW_LOG_ERROR, "Output ID: %s not found!", id);
		output->info.id			= bstrdup(id);
		output->private_output	= true;
	} else {
		output->info = *info;
		output->private_output = false;
	}

	if (0 != os_event_init(&output->stopping_event, OS_EVENT_TYPE_MANUAL))
		goto failed;
	if (0 != os_event_init(&output->reconnect_stop_event, OS_EVENT_TYPE_MANUAL))
		goto failed;

	/** initialize reconnect */
	output->reconnect_retry_sec = MGW_OUTPUT_RETRY_SEC;
	output->reconnect_retry_max = MGW_OUTPUT_RETRY_MAX;

	/** init control */
	output->control = bzalloc(sizeof(mgw_weak_output_t));
	output->control->output = output;

	/** initialize settings */
	if (!settings && info && info->get_default) {
		settings = mgw_data_create();
		settings = info->get_default();
	}

	/** initialize context */
	if (!mgw_context_data_init(&output->context,
				MGW_OBJ_TYPE_OUTPUT, settings, name, false))
		goto failed;
	mgw_context_data_insert(&output->context, &mgw->data.outputs_mutex,
					&mgw->data.first_output);

	/** initialize buffer */
	mgw_data_t *buf_settings = NULL;
	if (!mgw_data_has_user_value(settings, "buffer")) {
		buf_settings = mgw_rb_get_default();
		
		mgw_data_set_string(buf_settings, "io_mode", "read");
		mgw_data_set_string(buf_settings, "name", name);
		mgw_data_set_string(buf_settings, "id", id);

		mgw_data_set_obj(output->context.settings, "buffer", buf_settings);
	} else {
		buf_settings = mgw_data_get_default_obj(settings, "buffer");
	}

	output->buffer = mgw_rb_create(buf_settings, NULL);
	if (!output->buffer)
		goto failed;

	if (buf_settings)
		mgw_data_release(buf_settings);

	/** initlialize callback */
	output->active			= mgw_output_active;
	output->signal_stop		= mgw_output_signal_stop;
	output->source_is_ready = mgw_output_source_is_ready;
	output->get_encoder_settings = mgw_output_get_encoder_settings;
	output->get_video_header = mgw_output_get_video_header;
	output->get_audio_header = mgw_output_get_audio_header;
	output->get_next_encoder_packet = mgw_output_get_next_encoder_packet;

	if (info)
		output->context.data =
			info->create(output->context.settings, output);

	output->valid = true;
	blog(MGW_LOG_DEBUG, "Output %s (%s) created!", name, id);
	return output;

failed:
	mgw_output_destroy(output);
	return NULL;
}

void mgw_output_destroy(struct mgw_output *output)
{
	if (!output)
		return;

	blog(MGW_LOG_DEBUG, "Output %s destroyed!", output->context.name);

	if (output->valid && mgw_output_active(output))
		mgw_output_actual_stop(output, true);

	os_event_wait(output->stopping_event);
	if (output->context.data) {
        blog(MGW_LOG_INFO, "----------->> call '%s' stream destroy", output->info.id);
        output->info.destroy(output->context.data);
    }
	/** Destroy buffer */
	if (output->buffer)
		mgw_rb_destroy(output->buffer);

	os_event_destroy(output->stopping_event);
	os_event_destroy(output->reconnect_stop_event);

	/** destroy output resource */
	mgw_context_data_free(&output->context);

	if (output->private_output)
		bfree((void*)output->info.id);

	blog(MGW_LOG_INFO, "-------------->> free output!\n");
	bfree(output);
}

void mgw_output_addref(mgw_output_t *output)
{
	if (!output)
		return;
	mgw_ref_addref(&output->control->ref);
}

void mgw_output_release(mgw_output_t *output)
{
	if (!mgw) {
		blog(MGW_LOG_ERROR, "Tried to release a output but mgw core is NULL!");
		return;
	}

	if (!output)
		return;
	
	mgw_weak_output_t *ctrl = output->control;
	if (mgw_ref_release(&ctrl->ref)) {
		mgw_output_destroy(output);
		mgw_weak_output_release(ctrl);
	}
}

void mgw_weak_output_addref(mgw_weak_output_t *weak)
{
	if (!weak)
		return;

	mgw_weak_ref_addref(&weak->ref);
}

void mgw_weak_output_release(mgw_weak_output_t *weak)
{
		if (!weak)
		return;

	if (mgw_weak_ref_release(&weak->ref))
		bfree(weak);
}

mgw_output_t *mgw_output_get_ref(mgw_output_t *output)
{
		if (!output)
		return NULL;

	return mgw_weak_output_get_output(output->control);
}

mgw_weak_output_t *mgw_output_get_weak_output(mgw_output_t *output)
{
		if (!output)
		return NULL;

	mgw_weak_output_t *weak = output->control;
	mgw_weak_output_addref(weak);
	return weak;
}

mgw_output_t *mgw_weak_output_get_output(mgw_weak_output_t *weak)
{
	if (!weak)
		return NULL;

	if (mgw_weak_ref_get_ref(&weak->ref))
		return weak->output;

	return NULL;
}

bool mgw_weak_output_references_output(mgw_weak_output_t *weak,
		mgw_output_t *output)
{
	return weak && output && weak->output == output;
}

const char *mgw_output_get_name(const mgw_output_t *output)
{
	if (!output)
		return NULL;

	return output->context.name;
}

bool mgw_output_start(mgw_output_t *output)
{
	if (!output || (!output->private_output && \
			!output->context.data))
		return false;

	if (!output->buffer)
		return false;
    os_event_signal(output->stopping_event);
	return mgw_output_actual_start(output);
}

void mgw_output_stop(mgw_output_t *output)
{
	if (!output)
		return;
	mgw_output_actual_stop(output, true);
}

mgw_data_t *mgw_output_get_state(mgw_output_t *output)
{
    mgw_data_t *state_info = NULL;
	int push_state = 0;
    const char *username = NULL;
	bool authen = false;

    if (!output)
        return NULL;

	if (!mgw_output_source_is_ready(output))
		push_state = MGW_OUTPUT_STOPED;
	else if (mgw_output_active(output))
		push_state = MGW_OUTPUT_STREAMING;
	else if (reconnecting(output))
		push_state = MGW_OUTPUT_RECONNECTING;
	else if (mgw_output_source_is_ready(output) &&
			!mgw_output_active(output))
		push_state = MGW_OUTPUT_CONNECTING;

	username = mgw_data_get_string(output->context.settings, "username");
	if (username && strlen(username)) {
		blog(MGW_LOG_INFO, "user name:%s ", username);
		authen = true;
	}

    state_info = mgw_data_create();
    mgw_data_set_bool(state_info, "active", mgw_output_active(output));
	mgw_data_set_int(state_info, "state", push_state);
	mgw_data_set_int(state_info, "failed_cnt", os_atomic_load_long(&output->failed_count));
	mgw_data_set_bool(state_info, "authen", authen);

    return state_info;
}