#include "mgw.h"
#include "util/tlog.h"
#include "util/bmem.h"

extern struct mgw_core *mgw;

static inline bool stream_actived(mgw_stream_t *stream)
{
	return !!stream && os_atomic_load_bool(&stream->actived);
}

static int signal_stop_internal(void* opaque, struct call_params *params)
{

}

static int signal_reconnect_internal(void* opaque, struct call_params *params)
{

}

static int signal_started_internal(void* opaque, struct call_params *params)
{

}

static bool mgw_stream_init_context(struct mgw_stream *stream,
				mgw_data_t *settings, const char *name, bool is_private)
{
	if (!mgw_context_data_init(stream, &stream->context,
		MGW_OBJ_TYPE_STREAM, settings, name, is_private))
		return false;

	/** signal notify handlers register */
	proc_handler_t *handler = proc_handler_create(stream);
	proc_handler_add(handler, "stop",		signal_stop_internal);
	proc_handler_add(handler, "started",	signal_started_internal);
	proc_handler_add(handler, "reconnect",	signal_reconnect_internal);

	return true;
}

static void mgw_stream_destroy(struct mgw_stream *stream)
{
	if (!stream) return;

	if (stream->source)
		mgw_source_release(stream->source);
	if (stream->outputs_list)
		mgw_output_release_all(stream->outputs_list);

	pthread_mutex_destroy(&stream->outputs_mutex);
	mgw_context_data_free(&stream->context);

	bfree(stream);
}

mgw_stream_t *mgw_stream_create(mgw_device_t *device,
			const char *stream_name, mgw_data_t *settings)
{
	if (!stream_name || !settings) {
		tlog(TLOG_DEBUG, "Paramer invalid!");
		return NULL;
	}

	bool is_private = mgw_data_get_bool(settings, "is_private");
	struct mgw_stream *stream = bzalloc(sizeof(struct mgw_stream));
	if (0 != pthread_mutex_init(&stream->outputs_mutex, NULL))
		goto error;
	if (0 != pthread_mutex_init(&stream->outputs_whitelist_mutex, NULL))
		goto error;
	if (0 != pthread_mutex_init(&stream->outputs_blacklist_mutex, NULL))
		goto error;

	if (!mgw_stream_init_context(stream, settings, stream_name, is_private))
		goto error;

	stream->control = bzalloc(sizeof(struct mgw_ref));
	stream->control->data = stream;
	if (device && !is_private)
		mgw_context_data_insert(&stream->context,
			&device->stream_mutex, &device->stream_list);
	else
		mgw_context_data_insert(&stream->context,
			&mgw->data.priv_stream_mutex, &mgw->data.priv_streams_list);

	return stream;

error:
	bfree(stream->control);
	mgw_stream_destroy(stream);
	return NULL;
}

void mgw_stream_stop(mgw_stream_t *stream)
{
	if (stream) {
		
	}
}

void mgw_stream_addref(mgw_stream_t *stream)
{
	if (stream && stream->control)
		mgw_ref_addref(stream->control);
}

void mgw_stream_release(mgw_stream_t *stream)
{
	if (!stream || !stream->control) return;

	struct mgw_ref *ctrl = stream->control;
	if (mgw_ref_release(ctrl)) {
		mgw_stream_destroy(stream);
		bfree(ctrl);
	}
}

mgw_stream_t *mgw_stream_get_ref(mgw_stream_t *stream)
{
	if (!stream) return NULL;
	mgw_get_ref(stream->control) ?
		(mgw_stream_t*)stream->control->data : NULL;
}

mgw_stream_t *mgw_get_weak_stream(mgw_stream_t *stream)
{
	if (!stream) return NULL;
	return (mgw_stream_t*)stream->control->data;
}

bool mgw_stream_references_stream(struct mgw_ref *ref,
		mgw_stream_t *stream)
{
	return ref && stream && stream == stream->control;
}

const char *mgw_stream_get_name(const mgw_stream_t *stream)
{
	return !!stream ? stream->context.obj_name : NULL;
}

bool mgw_stream_has_source(mgw_stream_t *stream)
{
	return !!stream && !!stream->source;
}

int mgw_stream_add_source(mgw_stream_t *stream, mgw_data_t *source_settings)
{
	if (!stream || !source_settings)
		return MGW_ERR_EPARAM;
	if (stream->source) {
		tlog(TLOG_DEBUG, "Stream[%s] aready have a source[%s]\n");
		return mgw_src_err(MGW_ERR_INVALID_RES);
	}

	const char *source_name	= mgw_data_get_string(source_settings, "source_name");
	if (!(stream->source = mgw_source_create(stream, source_name, source_settings)))
		return mgw_src_err(MGW_ERR_INVALID_RES);

	if (!mgw_source_is_private(stream->source)) {
		if (!mgw_source_start(stream->source)) {
			tlog(TLOG_ERROR, "Start source %s failed!\n", source_name);
			return mgw_src_err(MGW_ERR_ESTARTING);
		}
	}

	os_atomic_set_bool(&stream->actived, true);

	/**< start all shitelist outputs and save them to outputlist */
	mgw_output_t *output = stream->outputs_whitelist;
	while (output) {
		mgw_output_t *next = output->context.next;
		if (!mgw_output_start(output))
			tlog(TLOG_ERROR, "Tried to start output failed!");

		mgw_context_data_insert(output, &stream->outputs_mutex, stream->outputs_list);
		mgw_context_data_remove(output);

		output = next;
	}

	stream->outputs_whitelist = NULL;
	return mgw_src_err(MGW_ERR_SUCCESS);
}

void mgw_stream_release_source(mgw_stream_t *stream)
{
	if (!stream) return;

	if (mgw_source_is_private(stream->source))
		mgw_source_stop(stream->source);

	mgw_source_release(stream->source);
	stream->source = NULL;
	os_atomic_set_bool(&stream->actived, false);
}

int mgw_stream_add_output(mgw_stream_t *stream, mgw_data_t *output_settings)
{
	bool success = false;
	if (!stream || !output_settings)
		return mgw_out_err(MGW_ERR_EPARAM);

	mgw_output_t *output = NULL;
	const char *output_name	= mgw_data_get_string(output_settings, "output_name");
	if (stream->outputs_list &&
		(output = mgw_get_output_by_name(stream->outputs_list,
							&stream->outputs_mutex, output_name))) {
		tlog(TLOG_DEBUG, "Stream[%s] aready have output[%s]\n",
								stream->context.obj_name, output_name);
		mgw_output_release(output);
		return mgw_out_err(MGW_ERR_EXISTED);
	}
	if (stream->outputs_whitelist &&
		(output = mgw_get_output_by_name(stream->outputs_whitelist,
							&stream->outputs_whitelist_mutex, output_name))) {
		tlog(TLOG_DEBUG, "Stream[%s] aready have output[%s]\n",
								stream->context.obj_name, output_name);
		mgw_output_release(output);
		return mgw_out_err(MGW_ERR_EXISTED);
	}

	if (!(output = mgw_output_create(stream, output_name, output_settings))) {
		tlog(TLOG_DEBUG, "Tried to create output[%s] failed!", output_name);
		return mgw_out_err(MGW_ERR_INVALID_RES);
	}

	if (stream_actived(stream) && !!stream->source &&
		!(success = mgw_output_start(output))) {

		int result = output->last_error_status;
		tlog(TLOG_INFO, "Tried to start output:%s failed, code:%d, try again!", output_name, result);
		mgw_output_destroy(output);
		return mgw_out_err(result);
	}

	// if (!stream->outputs_list)
	// 	stream->outputs_list = output;
	return mgw_out_err(MGW_ERR_SUCCESS);
}

void mgw_stream_release_output(mgw_stream_t *stream, mgw_output_t *output)
{
	if (!stream || !output) return;
	mgw_output_stop(output);
	mgw_output_release(output);
}

void mgw_stream_release_output_by_name(mgw_stream_t *stream, const char *name)
{
	if (!stream || !name) return;

	mgw_output_t *output = NULL;
	if ((output = mgw_get_weak_output_by_name(stream->outputs_list, 
					&stream->outputs_mutex, name))) {
		mgw_stream_release_output(stream, output);
	}
}

mgw_data_t *mgw_stream_get_output_info(mgw_stream_t *stream, const char *output_name)
{

}

mgw_data_t *mgw_stream_get_output_setting(mgw_stream_t *stream, const char *id)
{

}

bool mgw_stream_send_packet(mgw_stream_t *stream, struct encoder_packet *packet)
{
	if (!stream || !stream->source)
		return false;
	if (mgw_source_is_private(stream->source))
		mgw_source_write_packet(stream->source, packet);
	return true;
}