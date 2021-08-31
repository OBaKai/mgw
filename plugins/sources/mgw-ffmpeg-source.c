#include "mgw-internal.h"
#include "mgw-sources.h"
#include "util/bmem.h"
#include "util/dstr.h"
#include "util/base.h"
#include "util/tlog.h"
#include "util/threading.h"

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libswscale/swscale.h"

#define MGW_FF_SRC_NAME		"ffmpeg_source"
#define VBSF_H264			"h264_mp4toannexb"
#define VBSF_HEVC			"hevc_mp4toannexb"

#define ERROR_READ_THRESHOLD	3

static pthread_once_t ff_source_context_once = PTHREAD_ONCE_INIT;

struct ffmpeg_source {
    mgw_source_t        *source;

	volatile bool		actived;
	volatile bool		disconnected;
	os_event_t			*stop_event;
	os_event_t			*continue_read_event;
	pthread_t			receive_thread;
	volatile long		error_read;

	mgw_data_t			*setting;
	struct dstr			uri;

	bool				is_local_file;
	bool				is_looping;

	int					sws_width, sws_height;
	int					audio_index, video_index;
	AVFormatContext		*fmt_ctx;
	AVStream			*vst, *ast;
	AVBSFContext		*vbsf_ctx;
	struct SwsContext	*sws_ctx;

	bool				save_to_file;
	FILE				*audio_file, *video_file;
};

static inline bool stopping(struct ffmpeg_source *s)
{
	return os_event_try(s->stop_event) != EAGAIN;
}
 
static inline bool actived(struct ffmpeg_source *s)
{
	return os_atomic_load_bool(&s->actived);
}

static inline long error_reading(struct ffmpeg_source *s)
{
	return os_atomic_inc_long(&s->error_read) >= ERROR_READ_THRESHOLD;
}

static inline bool ffmpeg_source_valid(struct ffmpeg_source *s)
{
	return !!s && !!s->source;
}

void ffmpeg_log_callback(void *opaque, int level, const char *fmt, va_list arg)
{
	switch (level) {
		case AV_LOG_FATAL:		level = TLOG_FATAL;	break;
		case AV_LOG_ERROR:		level = TLOG_ERROR; break;
		case AV_LOG_WARNING: 	level = TLOG_WARN;	break;
		case AV_LOG_INFO:		level = TLOG_INFO;	break;
		case AV_LOG_DEBUG:		level = TLOG_DEBUG;	break;
		default: if (level < AV_LOG_FATAL) level = TLOG_FATAL; break;
	}
	static char buffer[1024] = {};
	memset(buffer, 0, sizeof(buffer));
	vsnprintf(buffer, sizeof(buffer), fmt, arg);
	tlog(level, "%s\n", buffer);
}

/**< Initialize ffmpeg all environment*/
static void init_once_thread(void)
{
	avformat_network_init();
	av_log_set_flags(AV_LOG_SKIP_REPEATED);
	av_log_set_level(AV_LOG_INFO);
	av_log_set_callback(ffmpeg_log_callback);
}

static const char *ffmpeg_source_get_name(void *type)
{
    UNUSED_PARAMETER(type);
    return MGW_FF_SRC_NAME;
}

static inline int do_proc_handler(struct ffmpeg_source *s,
				const char *name, call_params_t *params)
{
	proc_handler_t *handler = s->source->context.procs;
	return proc_handler_do(handler, name, params);
}

static void ffmpeg_source_destroy(void *data)
{
	struct ffmpeg_source *s = data;
	if (!ffmpeg_source_valid(s))
		return;

	if (actived(s)) {
		os_atomic_set_bool(&s->actived, false);
		pthread_join(s->receive_thread, NULL);
	}

	av_bsf_free(&s->vbsf_ctx);
	avformat_close_input(&s->fmt_ctx);
	if (s->sws_ctx)
		sws_freeContext(s->sws_ctx);

	os_event_destroy(s->stop_event);
	os_event_destroy(s->continue_read_event);
	if (s->setting)
		mgw_data_release(s->setting);
	if (!dstr_is_empty(&s->uri))
		dstr_free(&s->uri);
	if (s->video_file)
		fclose(s->video_file);
	if (s->audio_file)
		fclose(s->audio_file);
	bfree(s);
}

static int ffmpeg_interrupt_callback(void *opaque)
{
	struct ffmpeg_source *s = opaque;
	tlog(TLOG_DEBUG, "Received a ffmpeg interrupt!\n");
	return 0;
}

static int create_ffmpeg_media(struct ffmpeg_source *s, const char *uri)
{
	int ret = 0;

	if ((ret = avformat_open_input(&s->fmt_ctx, uri, NULL, NULL)))
		goto error;
	if ((ret = avformat_find_stream_info(s->fmt_ctx, NULL)))
		goto error;

	if ((ret = av_find_best_stream(s->fmt_ctx,
				AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0)) < 0)
		goto error;
	if (AV_CODEC_ID_AAC != s->ast->codecpar->codec_id) {
		tlog(TLOG_ERROR, "Couldn't support this audio codec:%d\n", s->ast->codecpar->codec_id);
		goto error;
	}

	s->audio_index = ret;
	s->ast = s->fmt_ctx->streams[ret];
	if (s->save_to_file && !s->audio_file)
		s->audio_file = fopen("source.aac", "wb+");

	if ((ret = av_find_best_stream(s->fmt_ctx,
				AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)))
		goto error;

	s->video_index = ret;
	s->vst = s->fmt_ctx->streams[ret];

	const char *vbsf_type = NULL, *v_ext = NULL;
	switch (s->vst->codecpar->codec_id) {
		case AV_CODEC_ID_H264: vbsf_type = VBSF_H264; v_ext = "h264"; break;
		case AV_CODEC_ID_HEVC: vbsf_type = VBSF_HEVC; v_ext = "hevc"; break;
		default: {
			ret  = AVERROR_BSF_NOT_FOUND;
			goto error;
		}
	}

	if (s->save_to_file && !s->video_file) {
		char *filename[32] = {};
		snprintf(filename, sizeof(filename), "video_source.%s", v_ext);
		s->video_file = fopen(filename, "wb+");
	}

	const AVBitStreamFilter *vbsf = av_bsf_get_by_name(vbsf_type);
	if ((ret = av_bsf_alloc(vbsf, &s->vbsf_ctx) < 0)) {
		tlog(TLOG_ERROR, "Tried to alloc bsf:%s failed!\n", vbsf_type);
		goto error;
	}
	ret = avcodec_parameters_copy(s->vbsf_ctx->par_in, s->vst->codecpar);
	ret = av_bsf_init(s->vbsf_ctx);

error:
	return ret;
}

static void *ffmpeg_source_create(mgw_data_t *setting, mgw_source_t *source)
{
	int ret;
	if (!setting || !source)
		return NULL;

    struct ffmpeg_source *s = bzalloc(sizeof(struct ffmpeg_source));
    s->source = source;
	s->setting = setting;

	if (0 != os_event_init(&s->stop_event, OS_EVENT_TYPE_MANUAL))
		goto error;
	if (0 != os_event_init(&s->continue_read_event, OS_EVENT_TYPE_MANUAL))
		goto error;
	if (0 != pthread_once(&ff_source_context_once, init_once_thread)) {
		blog(MGW_LOG_ERROR, "Create once thread to initialize ffmpeg failed!");
		goto error;
	}

	s->save_to_file = mgw_data_get_bool(setting, "save_file");
	const char *uri = mgw_data_get_string(setting, "uri");
	if (!uri) {
		tlog(TLOG_DEBUG, "couldn't find uri!\n");
		goto error;
	}

	if (0 != create_ffmpeg_media(s, uri)) {
		tlog(TLOG_ERROR, "Open uri:%s failed, error:%s\n", av_err2str(ret));
		goto error;
	}

	return s;

error:
	ffmpeg_source_destroy(s);
    return NULL;
}

#define AAC_SAMPLE_SIZE_MAX		256*1024
static void *read_thread(void *arg)
{
	struct ffmpeg_source *s = arg;
	AVPacket pkt = {}, bsf_pkt = {};
	int ret;
	char *audio_buffer = bzalloc(AAC_SAMPLE_SIZE_MAX);

	while (actived(s)) {
		if (stopping(s))
			break;

		struct encoder_packet packet = {};
		if ((ret = av_read_frame(s->fmt_ctx, &pkt)) < 0) {
			/** reach to end */
			if ((ret == AVERROR_EOF || avio_feof(s->fmt_ctx->pb)) && s->is_looping) {
				av_seek_frame(s->fmt_ctx, s->video_index, 0, AVSEEK_FLAG_BACKWARD);
			} else if (error_reading(s)) {
				os_atomic_set_bool(&s->disconnected, true);
				break;
			}
			os_event_timedwait(s->continue_read_event, 10);
			continue;
		}

		if (s->video_index == pkt.stream_index) {
			if (s->vbsf_ctx && 0 == av_bsf_send_packet(s->vbsf_ctx, &pkt)) {
				while (0 == av_bsf_receive_packet(s->vbsf_ctx, &bsf_pkt)) {
					if (s->video_file) {
						fwrite(bsf_pkt.data, bsf_pkt.size, 1, s->video_file);
						fflush(s->video_file);
					}
					tlog(TLOG_DEBUG, "write video data! size = %d, data[0]:%02x, "\
								"data[1]:%02x, data[2]:%02x, data[3]:%02x, data[4]:%02x",
								bsf_pkt.size, bsf_pkt.data[0], bsf_pkt.data[1], \
								bsf_pkt.data[2], bsf_pkt.data[3], bsf_pkt.data[4]);

					packet.type = ENCODER_VIDEO;
					packet.keyframe = mgw_avc_keyframe(bsf_pkt.data, bsf_pkt.size);
					packet.size = bsf_pkt.size;
					packet.data = bsf_pkt.data;
					packet.pts = packet.dts = (bsf_pkt.pts == AV_NOPTS_VALUE) ?
									NAN : bsf_pkt.pts * av_q2d(s->vst->time_base) * 1000000;
					s->source->output_packet(s->source, &packet);
					av_packet_unref(&bsf_pkt);
				}
			}
		} else if (s->audio_index == pkt.stream_index) {
			packet.type = ENCODER_AUDIO;
			packet.keyframe = false;
			packet.data = audio_buffer;
			packet.size = mgw_aac_add_adts(s->ast->codecpar->sample_rate,
							s->ast->codecpar->profile, s->ast->codecpar->channels,
							pkt.size, pkt.data, audio_buffer);
			packet.pts = packet.dts = (bsf_pkt.pts == AV_NOPTS_VALUE) ?
							NAN : bsf_pkt.pts * av_q2d(s->ast->time_base) * 1000000;
			s->source->output_packet(s->source, &packet);
			av_packet_unref(&pkt);
		}
	}

	if (os_atomic_load_bool(&s->disconnected))
		tlog(TLOG_INFO, "Disconnected from %s \n", s->uri.array);
	else
		tlog(TLOG_INFO, "User disconnected\n");

	if (!stopping(s) && os_atomic_load_bool(&s->disconnected))
		pthread_detach(s->receive_thread);

	/** Notify stop status by callback*/
	ret = MGW_DISCONNECTED;
	call_params_t params = {.in = &ret};
	do_proc_handler(s, "signal_stop", &params);

	bfree(audio_buffer);
	os_atomic_set_long(&s->error_read, 0);
	os_event_reset(s->stop_event);
	os_event_reset(s->continue_read_event);
	os_atomic_set_bool(&s->actived, false);

	return NULL;
}

static bool ffmpeg_source_start(void *data)
{
	struct ffmpeg_source *s = data;
    if (!ffmpeg_source_valid(s))
		return false;

	os_atomic_set_bool(&s->actived, true);
	return pthread_create(&s->receive_thread, NULL, read_thread, s) == 0;
}

static void ffmpeg_source_stop(void *data)
{
	struct ffmpeg_source *s = data;
	if (!ffmpeg_source_valid(s) ||
		stopping(s))
		return;

	if (actived(s)) {
		os_event_signal(s->stop_event);
		pthread_join(s->receive_thread, NULL);
	} else {
		int ret = MGW_SUCCESS;
		call_params_t params = {.in = &ret};
		do_proc_handler(s, "signal_stop", &params);
	}

	/**< reset some resources */

}

static void ffmpeg_source_get_defaults(mgw_data_t *settings)
{

}

static void ffmpeg_source_update(void *data, mgw_data_t *settings)
{

}

static mgw_data_t *ffmpeg_source_get_settings(void *data)
{
    return NULL;
}

static size_t ffmpeg_source_get_header(enum encoder_type type, uint8_t **data)
{

}

struct mgw_source_info ffmpeg_source_info = {
    .id                 = "ffmpeg_source",
    .output_flags       = MGW_SOURCE_AV |
						  MGW_SOUTCE_NETSTREAM |
						  MGW_SOURCE_LOCALFILE,
    .get_name           = ffmpeg_source_get_name,
    .create             = ffmpeg_source_create,
    .destroy            = ffmpeg_source_destroy,
    .start              = ffmpeg_source_start,
    .stop               = ffmpeg_source_stop,

    .get_defaults       = ffmpeg_source_get_defaults,
    .update             = ffmpeg_source_update,
    .get_settings       = ffmpeg_source_get_settings,
	.get_extra_data		= ffmpeg_source_get_header
};