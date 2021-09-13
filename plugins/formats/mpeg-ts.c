#include "mgw-internal.h"
#include "mgw-formats.h"

#include "util/base.h"
#include "util/tlog.h"
#include "util/bmem.h"
#include "util/dstr.h"
#include "util/mgw-data.h"

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avio.h"

#undef  MPEGTS_MODULE_NAME
#define MPEGTS_MODULE_NAME     "mpegts-de/mux"

struct mpegts_format {
	AVFormatContext		*fmt_ctx;
	AVOutputFormat		*out_fmt;
	AVStream			*audio_stream;
	AVStream			*video_stream;
	AVCodec				*vcodec, *acodec;

	int64_t				start_dts_offset;
	int64_t				last_dts;
	uint8_t				*out_buffer;
	struct bmem			extra_data;

	struct dstr			src_file;
	struct dstr			dst_file;

	int					flags;
	volatile bool		active;
	volatile bool 		disconnected;
	
	void				*opaque;
	proc_packet			write_packet;
	mgw_data_t			*settings;
};

static inline bool actived(struct mpegts_format *ts)
{
	return os_atomic_load_bool(&ts->active);
}

static inline bool disconnected(struct mpegts_format *ts)
{
	return os_atomic_load_bool(&ts->disconnected);
}

static const char *mpegts_get_name(void *type)
{
	return MPEGTS_MODULE_NAME;
}

static inline bool has_video(struct mpegts_format *ts)
{
	return !!ts->fmt_ctx && !!ts->video_stream;
}

static inline bool has_audio(struct mpegts_format *ts)
{
	return !!ts->fmt_ctx && !!ts->audio_stream;
}

static inline enum AVCodecID get_codec_id(const char *id)
{
	if (!strncasecmp(id, "h264", 4) ||
		!strncasecmp(id, "avc1", 4))
		return AV_CODEC_ID_H264;
	else if (!strncasecmp(id, "hevc", 4) ||
			 !strncasecmp(id, "h265", 4) ||
			 !strncasecmp(id, "hev1", 4))
		return AV_CODEC_ID_HEVC;
	else if (!strncasecmp(id, "aac", 3) || 
			 !strncasecmp(id, "mp4a", 4))
		return AV_CODEC_ID_AAC;
	else
		return AV_CODEC_ID_NONE;
}

static bool mpegts_add_video_stream(struct mpegts_format *ts)
{
	AVCodecContext *codec_ctx = NULL;
	const char *vencoder = mgw_data_get_string(ts->settings, "vencoderID");
	int bitrate = mgw_data_get_int(ts->settings, "vbps");
	int width = mgw_data_get_int(ts->settings, "width");
	int height = mgw_data_get_int(ts->settings, "height");
	int framerate = mgw_data_get_int(ts->settings, "fps");

	enum AVCodecID codec_id = AV_CODEC_ID_NONE;
	if (vencoder)
		codec_id = get_codec_id(vencoder);
	
	ts->vcodec = avcodec_find_encoder(codec_id);
	if (!ts->vcodec) {
		blog(MGW_LOG_ERROR, "could not found the video encoder(%d)!", codec_id);
	}

	ts->video_stream = avformat_new_stream(ts->fmt_ctx, NULL);
	if (!ts->video_stream) {
		blog(MGW_LOG_ERROR, "Tried to create a video stream for mpegts failed!");
		return false;
	}

	blog(MGW_LOG_INFO, "mpegts add video stream: id:%s, width:%d, height:%d, fps:%d, kbps:%d",
						vencoder, width, height, framerate, bitrate);

	codec_ctx = avcodec_alloc_context3(ts->vcodec);

	/*codec_ctx->codec_id		= codec_id;
	codec_ctx->bit_rate		= bitrate;
	codec_ctx->width		= width;
	codec_ctx->height		= height;
	codec_ctx->time_base	= (AVRational){1, framerate};
	codec_ctx->pix_fmt		= AV_PIX_FMT_YUV420P;
	ts->video_stream->time_base = codec_ctx->time_base;
	*/
	// AVOutputFormat *ofmt =  ts->fmt_ctx->oformat;
	// ofmt->video_codec 			 = codec_id;
	ts->video_stream->id		 = 0;//ts->fmt_ctx->oformat->video_codec;
	codec_ctx->codec_id          = codec_id;
	codec_ctx->codec_type        = AVMEDIA_TYPE_VIDEO;
	codec_ctx->pix_fmt           = AV_PIX_FMT_YUV420P;
	codec_ctx->bit_rate          = bitrate*1000;
	codec_ctx->width             = width;
	codec_ctx->height            = height;
	codec_ctx->time_base		 = (AVRational){1, 90000};
	ts->video_stream->time_base	 = (AVRational){1, framerate};
	//codec_ctx->extradata		 = (uint8_t*)bmemdup((const void *)ts->extra_data.array, ts->extra_data.len);
	//codec_ctx->extradata_size	 = ts->extra_data.len;

	avcodec_parameters_from_context(ts->video_stream->codecpar, codec_ctx);
	avcodec_free_context(&codec_ctx);

	// av_hex_dump(stdout, codec_ctx->extradata, codec_ctx->extradata_size);

	return true;
}

static bool mpegts_add_audio_stream(struct mpegts_format *ts)
{
	AVCodecContext *codec_ctx = NULL;
	const char *audio_encoder = mgw_data_get_string(ts->settings, "aencoderID");
	int audio_bitrate = mgw_data_get_int(ts->settings, "abps");
	int samplerate = mgw_data_get_int(ts->settings, "samplerate");
	int channels = mgw_data_get_int(ts->settings, "channels");
	int samplesize = mgw_data_get_int(ts->settings, "samplesize");

	enum AVCodecID codec_id = AV_CODEC_ID_NONE;
	if (audio_encoder)
		codec_id = get_codec_id(audio_encoder);

	ts->acodec = avcodec_find_encoder(codec_id);
	if (!ts->acodec) {
		blog(MGW_LOG_ERROR, "Could not found the encoder(%d)!", codec_id);
	}

	ts->audio_stream = avformat_new_stream(ts->fmt_ctx, NULL);
	if (!ts->audio_stream) {
		blog(MGW_LOG_ERROR, "Tried to create audio stream for mpegts failed!");
		return false;
	}

	codec_ctx = avcodec_alloc_context3(ts->acodec);

	blog(MGW_LOG_INFO, "abps(%d) samplerate(%d) samplesize(%d) channels(%d)",
		audio_bitrate, samplerate, samplesize, channels);

	// AVOutputFormat *ofmt = ts->fmt_ctx->oformat;
	// ofmt->audio_codec			 = codec_id;
	codec_ctx->sample_fmt 		 = codec_ctx->sample_fmt ? codec_ctx->sample_fmt : AV_SAMPLE_FMT_FLTP;
	ts->audio_stream->id         = ts->fmt_ctx->nb_streams - 1;//ts->fmt_ctx->oformat->audio_codec;
	// codec_ctx->frame_size        = 1024;
	codec_ctx->codec_id          = codec_id;
	codec_ctx->codec_type        = AVMEDIA_TYPE_AUDIO;
	codec_ctx->time_base		 = (AVRational){1, 1000};
	codec_ctx->bit_rate          = audio_bitrate;
	codec_ctx->sample_rate       = samplerate ? samplerate : 48000;
	codec_ctx->channel_layout    = (2 == channels) ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
	codec_ctx->channels          = \
						av_get_channel_layout_nb_channels(codec_ctx->channel_layout);
	ts->audio_stream->time_base        = (AVRational){ 1, codec_ctx->sample_rate };

	if (ts->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	uint8_t *header = NULL;
	codec_ctx->extradata_size = mgw_get_aac_lc_header(channels, samplesize, samplerate, &header);
	codec_ctx->extradata = header;

	avcodec_parameters_from_context(ts->audio_stream->codecpar, codec_ctx);
	avcodec_free_context(&codec_ctx);

	// av_hex_dump(stdout, codec_ctx->extradata, codec_ctx->extradata_size);

	return true;
}

static void mpegts_destroy(void *data)
{
	struct mpegts_format *ts = data;
	if (!ts)
		return;

	if (ts->fmt_ctx) {
		av_interleaved_write_frame(ts->fmt_ctx, NULL);
		av_write_trailer(ts->fmt_ctx);
		blog(MGW_LOG_INFO, "mpegts write trailer!");
	}

	if ((ts->flags & MGW_FORMAT_NO_FILE)) {
		av_freep(&ts->fmt_ctx->pb->buffer);
		avio_context_free(&ts->fmt_ctx->pb);
		avio_closep(&ts->fmt_ctx->pb);
	}

	if (ts->fmt_ctx)
		avformat_free_context(ts->fmt_ctx);

	dstr_free(&ts->src_file);
	dstr_free(&ts->dst_file);
	bmem_free(&ts->extra_data);

	if (ts->settings)
		mgw_data_release(ts->settings);
	blog(MGW_LOG_INFO, "Destroy mpegts!");
	bfree(ts);
}

static void *mpegts_create(mgw_data_t *settings, int flags,
					proc_packet write_packet, void *opaque)
{
	if (!write_packet && (flags & MGW_FORMAT_NO_FILE)) {
		blog(MGW_LOG_ERROR, "No write callback function and No flag to generate file");
		return NULL;
	}

	struct mpegts_format *ts = bzalloc(sizeof(struct mpegts_format));

	ts->flags 		 = flags;
	ts->opaque		 = opaque;
	ts->write_packet = write_packet;
	ts->settings 	 = mgw_data_newref(settings);
	os_atomic_set_bool(&ts->disconnected, false);
	dstr_copy(&ts->dst_file, "srt_test.ts");

	int ret = avformat_alloc_output_context2(&ts->fmt_ctx, NULL, "mpegts", ts->dst_file.array);
	if (ret < 0 || !ts->fmt_ctx) {
		blog(MGW_LOG_ERROR, "Tried to create AVFormatContext failed!");
		// avformat_alloc_output_context2(&ts->fmt_ctx, NULL, "mpeg", "1.mpg");
		goto error;
	}
	//snprintf(ts->fmt_ctx->filename, sizeof(ts->fmt_ctx->filename), "%s", ts->dst_file.array);

	if ((ts->flags & MGW_FORMAT_NO_FILE) && write_packet) {
		// const char *filename = outputformat;
		AVOutputFormat *ofmt = ts->fmt_ctx->oformat;
		ts->out_buffer = (uint8_t*)av_malloc(65536);
		AVIOContext *avio_out = avio_alloc_context(ts->out_buffer, 65536, 1, ts->opaque, NULL, ts->write_packet, NULL); 
		ts->fmt_ctx->pb = avio_out;
		ts->fmt_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;
		ts->fmt_ctx->flags |= AVFMT_FLAG_FLUSH_PACKETS;
		/**< Not create the file */
		// ofmt->flags |= AVFMT_NOFILE;

	} else if (!(ts->flags & MGW_FORMAT_NO_FILE) && !dstr_is_empty(&ts->dst_file)) {
		if (avio_open(&ts->fmt_ctx->pb, ts->dst_file.array, AVIO_FLAG_WRITE) < 0) {
			blog(MGW_LOG_ERROR, "Could not open '%s'\n", ts->dst_file.array);
			goto error;
		}
	}
	ts->out_fmt = ts->fmt_ctx->oformat;

	return ts;

error:
	mpegts_destroy(ts);
	return NULL;
}

static bool mpegts_start(void *data)
{
	struct mpegts_format *ts = data;
	if (!ts)
		return false;

	ts->last_dts			= 0;
	ts->start_dts_offset	= 0;
	
	if (disconnected(ts)) {
		os_atomic_set_bool(&ts->disconnected, false);
		return true;
	}

	if (!mpegts_add_video_stream(ts))
		return false;
	if (!mpegts_add_audio_stream(ts))
		return false;

	av_dump_format(ts->fmt_ctx, 0, ts->dst_file.array, 1);
	if (avformat_write_header(ts->fmt_ctx, NULL) < 0) {
		blog(MGW_LOG_ERROR, "Tried to write mpegts header failed!");
		return false;
	}
	os_atomic_set_bool(&ts->active, true);

	blog(MGW_LOG_INFO, "wirte mpegts header success!");
	return true;
}

static void mpegts_stop(void *data)
{
	struct mpegts_format *ts = data;
	if (!ts) return;
	blog(MGW_LOG_INFO, "stop mpegts!");
	os_atomic_set_bool(&ts->disconnected, true);
}

static size_t mpegts_send_packet(void *data, struct encoder_packet *packet)
{
	int ret = -1;
	struct mpegts_format *ts = data;
	const AVRational time_base = (AVRational){1, 1000};
	AVStream *stream = NULL;
	if (!ts || !actived(ts) ||
		!packet || disconnected(ts))
		return -1;

	AVPacket snd_packet = {};
	av_init_packet(&snd_packet);

	if (packet->keyframe && packet->type == ENCODER_VIDEO)
		snd_packet.flags |= AV_PKT_FLAG_KEY;

	if (packet->type == ENCODER_VIDEO)
		stream = ts->video_stream;
	else if (packet->type == ENCODER_AUDIO)
		stream = ts->audio_stream;

	snd_packet.stream_index = stream->index;
	packet->pts /= 1000;
	snd_packet.data = packet->data;
	snd_packet.size = packet->size;
	snd_packet.pos = -1;
	if (!ts->start_dts_offset)
		ts->start_dts_offset = packet->pts;

	snd_packet.pts = snd_packet.dts = packet->pts - ts->start_dts_offset;
	if (snd_packet.dts == ts->last_dts && ts->last_dts){
		ts->last_dts = snd_packet.dts;
		snd_packet.dts++;
		snd_packet.pts++;
	} else {
		ts->last_dts = snd_packet.dts;
	}

	av_packet_rescale_ts(&snd_packet, time_base, stream->time_base);

	// blog(MGW_LOG_INFO, "packet index:%d data[0]:%02x,data[1]:%02x,data[2]:%02x,data[3]:%02x,data[4]:%02x",
	// 			snd_packet.stream_index, snd_packet.data[0],snd_packet.data[1],
	// 			snd_packet.data[2],snd_packet.data[3],snd_packet.data[4]);

	ret = av_interleaved_write_frame(ts->fmt_ctx, &snd_packet);
	if (ret < 0) {
		blog(MGW_LOG_ERROR, "Send a frame data to mpegts container failed, error:'%s' ", av_err2str(ret));
	}
	av_packet_unref(&snd_packet);
	return ret;
}

static mgw_data_t *mpegts_get_settings(void* data)
{
	struct mpegts_format *ts = data;
	if (!ts) return NULL;

	return mgw_data_newref(ts->settings);
}

static mgw_data_t *mpegts_get_default(void)
{
	mgw_data_t *def_settings = mgw_data_create();

	mgw_data_set_string(def_settings, "vencoder", "h264");
	mgw_data_set_string(def_settings, "aencoder", "aac");

	return def_settings;
}

static void mpegts_update(void *data, mgw_data_t *settings)
{
	struct mpegts_format *ts = data;
	if (!data || !settings)
		return;

	mgw_data_release(ts->settings);
	ts->settings = mgw_data_newref(settings);
}

static void mpegts_set_extra_data(void *data, const uint8_t *extra_data, size_t size)
{
	struct mpegts_format *ts = data;
	if (!ts || !extra_data || size <= 0) return;

	bmem_copy(&ts->extra_data, (const char *)extra_data, size);
}

struct mgw_format_info mpegts_format_info = {
	.id				= "mpegts_format",
	.get_name		= mpegts_get_name,
	.create			= mpegts_create,
	.destroy		= mpegts_destroy,
	.start			= mpegts_start,
	.stop			= mpegts_stop,
	.send_packet	= mpegts_send_packet,

	.get_settings	= mpegts_get_settings,
	.get_default	= mpegts_get_default,
	.update			= mpegts_update,
	.set_extra_data = mpegts_set_extra_data
};