#include "mgw.h"
#include "util/tlog.h"
#include "util/bmem.h"

static bool mgw_stream_init_context(struct mgw_stream *stream,
				mgw_data_t *settings, const char *name, bool is_private)
{
	if (!mgw_context_data_init(&stream->context,
		MGW_OBJ_TYPE_STREAM, settings, name, is_private))
		return false;

	/** signal notify handlers register */

	return true;
}

mgw_stream_t *mgw_stream_create(const char *id, mgw_data_t *settings)
{
	if (!id || !settings) {
		tlog(TLOG_DEBUG, "Paramer invalid!");
		return NULL;
	}

	bool is_private = mgw_data_get_bool(settings, "is_private");
	struct mgw_stream *stream = bzalloc(sizeof(struct mgw_stream));
	if (0 != pthread_mutex_init(&stream->outputs_mutex, NULL))
		goto error;

	if (!mgw_stream_init_context(stream, settings, id, is_private))
		goto error;

	stream->control = bzalloc(sizeof(struct mgw_weak_stream));
	stream->control->stream = stream;

	return stream;

error:
	return NULL;
}

void mgw_stream_addref(mgw_stream_t *stream)
{

}

void mgw_stream_release(mgw_stream_t *stream)
{

}

mgw_stream_t *mgw_stream_get_ref(mgw_stream_t *stream)
{

}

mgw_stream_t *mgw_weak_stream_get_stream(mgw_weak_stream_t *weak)
{

}

bool mgw_weak_stream_references_stream(mgw_weak_stream_t *weak,
		mgw_stream_t *stream)
{

}

const char *mgw_stream_get_name(const mgw_stream_t *stream)
{

}

bool mgw_stream_add_source(mgw_stream_t *stream,
		const char *source_id, mgw_data_t *source_settings)
{

}

void mgw_stream_release_source(mgw_stream_t *stream, mgw_source_t *source)
{

}

void mgw_stream_release_source_by_id(mgw_stream_t *stream, const char *source_id)
{

}

bool mgw_stream_add_output(mgw_stream_t *stream,
		const char *output_id, mgw_data_t *output_settings)
{
	// struct mgw_output *output = mgw_output_create(output_id, stream->context.name);
	// mgw_context_data_insert(&output->context, &mgw->data.outputs_mutex,
	// 				&mgw->data.first_output);

	return true;
}

void mgw_stream_release_output(mgw_stream_t *stream, mgw_output_t *output)
{

}

void mgw_stream_release_output_by_id(mgw_stream_t *stream, const char *id)
{

}

mgw_data_t *mgw_stream_get_info(mgw_stream_t *stream)
{

}

mgw_data_t *mgw_stream_get_output_setting(mgw_stream_t *stream, const char *id)
{

}

void mgw_stream_signal_output_stop(mgw_stream_t *stream, char *output_id)
{

}

void mgw_stream_signal_output_reconnecting(mgw_stream_t *stream, char *output_id)
{

}

void mgw_stream_signal_output_connected(mgw_stream_t *stream, char *output_id)
{

}

void mgw_stream_signal_source_stop(mgw_stream_t *stream, char *output_id)
{

}

void mgw_stream_signal_source_reconnecting(mgw_stream_t *stream, char *output_id)
{

}

void mgw_stream_signal_source_connected(mgw_stream_t *stream, char *output_id)
{

}

bool mgw_stream_send_packet(mgw_stream_t *stream, struct encoder_packet *pkt)
{

}