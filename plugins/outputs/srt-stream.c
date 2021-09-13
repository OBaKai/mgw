#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include "mgw-internal.h"
#include "mgw-outputs.h"

#include "util/base.h"
#include "util/tlog.h"
#include "util/dstr.h"
#include "util/platform.h"
#include "util/threading.h"
#include "util/callback-handle.h"
#include "formats/mgw-formats.h"

#include "thirdparty/mgw-libsrt.h"

#undef  SRT_MODULE_NAME
#define SRT_MODULE_NAME     "srt_stream"

#define	SRT_DROP_THRESHOLD	3
#define SRT_ERROR_THRESHOLD	100
#define SRT_MAX_PACKET_SIZE	564000

struct srt_stream {
	mgw_output_t			*output;
	mgw_data_t				*settings;

	struct mgw_format_info	*mpegts_info;
	void					*mpegts;
	void					*srt_context;

	uint8_t					send_error_cnt;
	uint64_t				total_sent_bytes;
	uint64_t				total_sent_frames;
	int64_t					last_dts;
	os_event_t				*stop_event;
	int						last_error_code;
	int						failed_sent;

	volatile bool			active;
	volatile bool			disconnected;
	pthread_t				send_thread;

	uint8_t					*left_data;
	uint32_t				left_size;

	uint8_t					*frame_buffer;
	struct dstr				uri;
};

static bool active(struct srt_stream *stream)
{
	return os_atomic_load_bool(&stream->active);
}

static inline bool stopping(struct srt_stream *stream)
{
	return os_event_try(stream->stop_event) != EAGAIN;
}

static bool disconnected(void *opaque)
{
	struct srt_stream *stream = opaque;
	if (!stream)
		return true;
	return os_atomic_load_bool(&stream->disconnected);
}

static const char *srt_stream_get_name(void *type)
{
	UNUSED_PARAMETER(type);
	return SRT_MODULE_NAME;
}

static inline int do_source_proc_handler(struct srt_stream *stream,
				const char *name, call_params_t *params) {
	proc_handler_t *handler = stream->output->get_source_proc_handler(stream->output);
	return proc_handler_do(handler, name, params);
}

static inline int do_output_proc_handler(struct srt_stream *stream,
				const char *name, call_params_t *params) {
	proc_handler_t *handler = stream->output->context.procs;
	return proc_handler_do(handler, name, params);
}

int srt_stream_proc_packet(void *opaque, uint8_t *buf, int buf_size)
{
	int left_size = buf_size, ret = 0, send_seek = 0;
	uint8_t *send_data = buf;
	struct srt_stream *stream = opaque;
	if (!stream || !stream->left_data) return -1;

	if (!active(stream) || stopping(stream) ||
		disconnected(stream))
		return -1;

	if (stream->send_error_cnt > SRT_ERROR_THRESHOLD) {
		blog(MGW_LOG_ERROR, "Send error too much! error count:%d", stream->send_error_cnt);
		stream->send_error_cnt = 0;
		goto error;
	}

	int payload_size = mgw_libsrt_get_payload_size(stream->srt_context);

	/**< Send the remaining data from last time */
	if (stream->left_size) {
		int cp_size = buf_size;
		if (buf_size > (SRT_MAX_PACKET_SIZE - stream->left_size)) {
			blog(MGW_LOG_ERROR, "the mpegts packet is too big!, size:%d, max:%d", buf_size, SRT_MAX_PACKET_SIZE);
			cp_size = SRT_MAX_PACKET_SIZE - stream->left_size;
		}
		memcpy(stream->left_data + stream->left_size, buf, cp_size);
		left_size = stream->left_size + cp_size;
		send_data = stream->left_data;
		stream->left_size = 0;
	}

	// if ((stream->total_sent_frames % 20) == 0)
	// 	blog(MGW_LOG_INFO, "srt stream sending mpegts stream... packet:%llu, bytes:%llu",
	// 					stream->total_sent_frames, stream->total_sent_bytes);

	/**< mgw_libsrt_write send data size must be the settings payload size */
	while (left_size > payload_size) {
		ret = mgw_libsrt_write(stream->srt_context, (const uint8_t*)send_data + send_seek, payload_size);
		if (-5009 == ret || -2001 == ret || -5004 == ret ||
			-17 == ret || (-90 >= ret && ret >= -113))
		{
			blog(MGW_LOG_ERROR, "Wow! special error seding, ret:%d", ret);
			goto error;
		} else if (ret < 0) {
			stream->send_error_cnt++;
			blog(MGW_LOG_ERROR, "Error occur， will count it, ret:%d", ret);
		} else
			stream->total_sent_bytes += payload_size;

		send_seek += payload_size;
		left_size -= payload_size;
	}

	/**< send data left, but not greater than payload size 
	 * 	 Find out all full TS package sizes and send them */
	if (left_size > 0) {
		int full_ts_sizes = 0;
		int ts_left = left_size % MPEGTS_FIX_SIZE;
		full_ts_sizes = ts_left ? left_size - ts_left : left_size;
		if (full_ts_sizes > 0) {
			ret = mgw_libsrt_write(stream->srt_context, (const uint8_t*)send_data + send_seek, full_ts_sizes);
			if (-5009 == ret || -2001 == ret || -5004 == ret ||
				-17 == ret || (-90 >= ret && ret >= -113))
				goto error;
			else if (ret < 0)
				stream->send_error_cnt++;
			else
				stream->total_sent_bytes += payload_size;

			send_seek += full_ts_sizes;
		}

		/**< The left data that not full a ts packet must save and be sent at next sending */
		if (ts_left > 0) {
			memmove(stream->left_data, send_data + send_seek, ts_left);
			stream->left_size = ts_left;
		}
	}

	return buf_size;

error:
	blog(MGW_LOG_INFO, "Error sending and will restart srt stream");
	os_atomic_set_bool(&stream->disconnected, true);
	return ret;
}

static void srt_stream_destroy(void *data)
{
	struct srt_stream *stream = data;
	if (!stream)
		return;

	blog(MGW_LOG_INFO, "Receive destroy srt stream message!");

	if (active(stream) && !stopping(stream))
		os_event_signal(stream->stop_event);

	if (stopping(stream) && active(stream))
		pthread_join(stream->send_thread, NULL);

	mgw_data_release(stream->settings);
	if (stream->mpegts_info/* && stream->mpegts*/) {
		//stream->mpegts_info->destroy(stream->mpegts);
		bfree(stream->mpegts_info);
	}
	mgw_libsrt_destroy(stream->srt_context);
	dstr_free(&stream->uri);
	os_event_destroy(stream->stop_event);
	bfree(stream->left_data);
	bfree(stream->frame_buffer);
	bfree(stream);
}

static void *srt_stream_create(mgw_data_t *setting, mgw_output_t *output)
{
	extern struct mgw_format_info mpegts_format_info;

	if (!setting || !output)
		return NULL;

	struct srt_stream *stream = bzalloc(sizeof(struct srt_stream));
	stream->left_data = bzalloc(SRT_MAX_PACKET_SIZE);
	stream->frame_buffer = bzalloc(MGW_MAX_PACKET_SIZE);
	stream->output = output;
	stream->settings = setting;

	if (0 != os_event_init(&stream->stop_event, OS_EVENT_TYPE_MANUAL))
		goto error;

	/**< Create mpegts for srt */
	call_params_t params = {};
	if (0 != do_source_proc_handler(stream, "get_encoder_settings", &params)) {
		tlog(TLOG_ERROR, "Couldn't get encoder settings!");
		goto error;
	}
	stream->mpegts_info = bzalloc(sizeof(struct mgw_format_info));
	memcpy(stream->mpegts_info, &mpegts_format_info, sizeof(struct mgw_format_info));
	stream->mpegts = stream->mpegts_info->create((mgw_data_t *)params.out, \
						MGW_FORMAT_NO_FILE, srt_stream_proc_packet, stream);
	mgw_data_release((mgw_data_t *)params.out);

	/**< Create libsrt context */
	const char *uri = mgw_data_get_string(stream->settings, "path");
	if (!uri) {
		tlog(TLOG_ERROR, "SRT stream uri is NULL!");
		goto error;
	}
	dstr_copy(&stream->uri, uri);

	srt_int_cb *interrupt_cb = bzalloc(sizeof(srt_int_cb));
	interrupt_cb->callback = disconnected;
	interrupt_cb->opaque = (void *)stream;
	stream->srt_context = mgw_libsrt_create(interrupt_cb, uri, SRT_MODE_CALLER);
	if (!stream->srt_context) {
		tlog(TLOG_ERROR, "Tried to create srt context failed!\n");
		bfree(interrupt_cb);
		goto error;
	}

	return stream;

error:
	srt_stream_destroy(stream);
	return NULL;
}

static int init_connect(struct srt_stream *stream)
{
	bool success = false;
	if (dstr_is_empty(&stream->uri)) {
		tlog(TLOG_ERROR, "Tried to open srt but uri is NULL!");
		return MGW_BAD_PATH;
	}

	os_atomic_set_bool(&stream->disconnected, false);
	stream->total_sent_bytes	= 0;
	stream->total_sent_frames	= 0;
	stream->left_size			= 0;
	stream->send_error_cnt		= 0;

	/**< mpegts startup */
	if (stream->mpegts_info) {
		if (!stream->mpegts) {
			call_params_t params = {};
			if (0 != do_source_proc_handler(stream, "get_encoder_settings", &params)) {
				tlog(TLOG_ERROR, "Couldn't get encoder settings!");
				return MGW_ERROR;
			}
			stream->mpegts = stream->mpegts_info->create((mgw_data_t *)params.out, \
							MGW_FORMAT_NO_FILE, srt_stream_proc_packet, stream);
			mgw_data_release((mgw_data_t *)params.out);
		}
		success = stream->mpegts_info->start(stream->mpegts);
		if (!success) {
			blog(MGW_LOG_ERROR, "Srt stream start mpegts failed!");
			return MGW_INVALID_STREAM;
		}
	}

	tlog(TLOG_INFO, "Connect to srt uri: %s ...", stream->uri.array);
	/**< SRT is base on UDT and UDT is base on UDP, startup is fast, just one TTL */
	int ret = mgw_libsrt_open(stream->srt_context, stream->uri.array, SRT_IO_FLAG_WRITE);
	if (0 != ret) {
		tlog(TLOG_ERROR, "Tried to open srt(%s) failed! ret = %d, try it again", stream->uri.array, ret);
		return MGW_CONNECT_FAILED;
	}
	tlog(TLOG_INFO, "Connect to srt uri:'%s' success!", stream->uri.array);
	int64_t maxbw = ((6144+128) * 5000) / 16;
	mgw_libsrt_set_maxbw(stream->srt_context, maxbw);

	return MGW_SUCCESS;
}

static void *connect_and_send_thread(void *arg)
{
	struct srt_stream *stream = arg;
	int ret = 0;

	os_set_thread_name("srt stream: connect_and_send_thread");

	if ((ret = init_connect(stream)) != MGW_SUCCESS) {
		tlog(TLOG_ERROR, "Tried to connect srt failed, ret[%d]", ret);
		goto error;
	} else {
		call_params_t param = {};
		do_output_proc_handler(stream, "signal_started", &param);
	}

	os_atomic_set_bool(&stream->active, true);
	os_set_thread_name("srt-stream: connect and send thread");
	while(active(stream)) {
		if (stopping(stream) || disconnected(stream))
			break;

		struct encoder_packet packet = {};
		packet.data = stream->frame_buffer;
		if (stream->output->get_encoder_packet(
						stream->output, &packet) <= 0) {
			usleep(5 *1000);
			continue;
		}

		// int64_t ts_gap = packet.pts / 1000 - stream->last_dts;
		// if (stream->last_dts && ts_gap > 200) {
		// 	tlog(TLOG_ERROR, "srt stream packet pts too big!, last:%lld us, cur:%lld us, gap:%lld ms\n",
		// 				stream->last_dts, packet.pts, ts_gap);
		// }

		if ((ret = stream->mpegts_info->send_packet(stream->mpegts, &packet)) < 0) {
			usleep(5*1000);
			continue;
		}
		stream->last_dts = packet.pts / 1000;
		stream->total_sent_frames++;
		if ((stream->total_sent_frames % 6) == 0)
			usleep(10*1000);
	}

	if (disconnected(stream)) {
		blog(MGW_LOG_INFO, "Disconnected from %s", stream->uri.array);
		ret = MGW_DISCONNECTED;
	} else {
		blog(MGW_LOG_INFO, "User stopped the stream");
	}

error:
	stream->output->last_error_status = stream->last_error_code;
	mgw_libsrt_close(&stream->srt_context);
	/**< reconect srt need to recreate mpegts, here destroy it */
	stream->mpegts_info->stop(stream->mpegts);
	stream->mpegts_info->destroy(stream->mpegts);
	stream->mpegts = NULL;

	/**< Not User stop the stream, must detach the send thread and exit automatically */
	if (!stopping(stream)) {
		pthread_detach(stream->send_thread);
		blog(MGW_LOG_INFO, "srt stream signal stop!");
		call_params_t params = {.in = &ret};
		do_output_proc_handler(stream, "signal_stop", &params);
	}
	os_event_reset(stream->stop_event);
	os_atomic_set_bool(&stream->active, false);
	return NULL;
}

static bool srt_stream_start(void *data)
{
	struct srt_stream *stream = data;
	if (!stream)
		return false;

	if (!do_output_proc_handler(stream, "source_ready", NULL)) {
		tlog(TLOG_WARN, "Source are not ready when startup srt stream!");
		return false;
	}

	return pthread_create(&stream->send_thread, NULL, connect_and_send_thread, stream) == 0;
}

static void srt_stream_stop(void *data)
{
	struct srt_stream *stream = data;
	if (!stream || stopping(stream))
		return;

	if (active(stream))
		os_event_signal(stream->stop_event);
	else {
		tlog(TLOG_INFO, "srt stream stop signal stop, ret:%d", MGW_SUCCESS);
		int ret = MGW_SUCCESS;
		call_params_t params = {.in = &ret};
		do_output_proc_handler(stream, "signal_stop", &params);
	}

	stream->last_dts = 0;
	stream->left_size = 0;
}

static uint64_t srt_stream_get_total_bytes(void *data)
{
	struct srt_stream *stream = data;
	if (!stream)
		return 0;
	return stream->total_sent_bytes;
}

static mgw_data_t *srt_stream_get_default(void)
{
	mgw_data_t *settings = mgw_data_create();

	return settings;
}

static mgw_data_t *srt_stream_get_settings(void *data)
{
	struct srt_stream *stream = data;
	if (!stream)
		return NULL;

	return mgw_data_newref(stream->settings);
}

/**< only accept the maxbw, payload size to update*/
static void srt_stream_apply_update(void *data, mgw_data_t *settings)
{
	struct srt_stream *stream = data;
	if (!stream || settings)
		return;

	int maxbw = mgw_data_get_int(settings, "maxbw");
	int payloadsize = mgw_data_get_int(settings, "payloadsize");

	if (maxbw > 20) {
		mgw_libsrt_set_maxbw(stream->srt_context, maxbw);
		mgw_data_set_int(stream->settings, "maxbw", maxbw);
	}

	if (payloadsize > 188) {
		mgw_libsrt_set_payload_size(stream->srt_context, payloadsize);
		mgw_data_set_int(stream->settings, "payloadsize", payloadsize);
	}
}

public_visi struct mgw_output_info srt_output_info = {
	.id                 = "srt_output",
	.flags              = MGW_OUTPUT_AV |
						  MGW_OUTPUT_ENCODED,
	.get_name           = srt_stream_get_name,
	.create             = srt_stream_create,
	.destroy            = srt_stream_destroy,
	.start              = srt_stream_start,
	.stop               = srt_stream_stop,
	.get_total_bytes    = srt_stream_get_total_bytes,

	.get_default        = srt_stream_get_default,
	.get_settings       = srt_stream_get_settings,
	.update             = srt_stream_apply_update
};