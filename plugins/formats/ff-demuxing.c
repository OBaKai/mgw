/**
 * File: ff-demuxing.c
 * 
 * Description: Demux stream from local file or netstream by ffmpeg
 * 
 * Datetime: 2021/5/10
 * */
#include "ff-demuxing.h"
#include "util/dstr.h"
#include "util/bmem.h"
#include "util/base.h"
#include "util/threading.h"

#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include "libavutil/time.h"
#include "libavutil/bprint.h"
#include "libavutil/log.h"
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "libswresample/swresample.h"

#include <stdio.h>
#include <unistd.h>

struct ff_demux {
	struct dstr			src_file;

	AVFormatContext		*fmt;
	AVInputFormat		*ifmt;
	AVStream			*video;
	AVStream			*audio;
	AVPacket			pkt;
	AVBSFContext		*video_filter_ctx;
	FILE				*h264_file, *aac_file;

	int					video_index;
	int					audio_index;

	pthread_t			demux_thread;
	os_sem_t         	*demux_sem;
	os_event_t			*demux_stopping;
	volatile bool		active;
	bool				cycle_demux;

	void				*param;
	void				(*proc_packet)(void *param, struct encoder_packet *packet);
};

/** step: 
 * 1.avformat_open_input(fmt, src_file, NULL, NULL)
 * 2.avformat_find_stream_info(fmt, NULL)
 * 3.find video stream index and copy avcodec_parameters to context
 * 4.video: AVStream, fopen video file
 * 
 * 5.find audio stream index and copy avcodec_parameters to context
 * 6.audio: AVStream, fopen video file
 * 7.av_dump_format()
 * 8.av_init_packet()
 * 9.while(av_read_frame(fmt, &pkt))
 */

void *ff_demux_create(const char *url, bool save_file)
{
    int ret = 0;
    if (!url)
        return NULL;
	AVFormatContext *fmtctx = NULL;
	av_log_set_level(AV_LOG_TRACE);
	avdevice_register_all();
	avformat_network_init();

	struct ff_demux *demux = bzalloc(sizeof(struct ff_demux));
	dstr_copy(&demux->src_file, url);
	os_event_init(&demux->demux_stopping, OS_EVENT_TYPE_MANUAL);
	os_sem_init(&demux->demux_sem, 0);

	fmtctx = avformat_alloc_context();
	if ((ret = avformat_open_input(&fmtctx, url, NULL, NULL)) < 0) {
		blog(MGW_LOG_ERROR, "Tried to open input:%s failed, error:%s", url, av_err2str(ret));
		goto error;
	}

	if (avformat_find_stream_info(fmtctx, NULL) < 0) {
		blog(MGW_LOG_ERROR, "Tried to find stream info failed!");
		goto error;
	}
	av_dump_format(fmtctx, 0, url, 0);
	// demux->fmt = fmtctx;
	if ((ret = av_find_best_stream(fmtctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0) {
		blog(MGW_LOG_INFO, "Couldn't found video stream!");
	} else {
		demux->video_index = ret;
		demux->video = demux->fmt->streams[ret];
		if (!demux->h264_file && save_file)
			demux->h264_file = fopen("ff_demux_test.h264", "wb");
		blog(MGW_LOG_INFO, "video width:%d, height:%d\n",
				demux->video->codecpar->width, demux->video->codecpar->height);
	}

	if ((ret = av_find_best_stream(demux->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0)) < 0) {
		blog(MGW_LOG_INFO, "Couldn't found audio stream!");
	} else {
		demux->audio_index = ret;
		demux->audio = demux->fmt->streams[ret];
		if (!demux->aac_file && save_file)
			demux->aac_file = fopen("ff_demux_test.aac", "wb");
	}

	/** Save video and audio to file must be initialize filter and open file*/
	const AVBitStreamFilter *h264_filter = av_bsf_get_by_name("h264_mp4toannexb");
	if (av_bsf_alloc(h264_filter, &demux->video_filter_ctx) < 0) {
		blog(MGW_LOG_INFO, "Tried to alloc h264 filter failed!");
		if (demux->h264_file) {
			fclose(demux->h264_file);
			demux->h264_file = NULL;
		}
	} else {
		AVCodecParameters *codecpar = NULL;
		codecpar = demux->fmt->streams[demux->video_index]->codecpar;
		if (codecpar)
			avcodec_parameters_copy(demux->video_filter_ctx->par_in, codecpar);

		av_bsf_init(demux->video_filter_ctx);
	}

	av_dump_format(demux->fmt, 0, url, 0);

	av_init_packet(&demux->pkt);
	demux->pkt.data = NULL;
	demux->pkt.size = 0;

	demux->cycle_demux = true;

	return demux;

error:
	ff_demux_destroy(demux);
    return NULL;
}

static inline bool stopping(struct ff_demux *demux)
{
	return os_event_try(demux->demux_stopping) != EAGAIN;
}

/** ISO/IEC 2001 1.A.2.2.1 Fixed Header of ADTS  */
static inline size_t aac_add_adts_header(size_t size, char *out)
{
	// bslbf: bit string, left bit first
	// uimsbf: Unsigned integer, most significant bit first
	//vlclbf: cariable length code, left bit first
	#if 0
	int profile = 2;	//AAC LC
	int freqIdx = 4;	//44100 Hz
	int chanCfg = 2;	//2 channels

	memset(out, 0, sizeof(out));

	out[0] = 0xff;
	out[1] = 0xf1;
	/** profile:2, freqIdx:4, privBit:1, chnCfg:1-total-3 */
	out[2] = (uint8_t)(((profile) & 0x03) | ((freqIdx << 2) & 0x3c) | ((chanCfg & 0x1) << 7));
	/** chnCfg:2-total-3, origin/copy:1, home:1, var-bit:1, start:1, size:2-total-13 */
	out[3] = (uint8_t)(((chanCfg & 0x06) >> 1) | ((size & 0x3) << 6));
	/** size:8-total-13 */
	out[4] = (uint8_t)(size >> 2);
	/** size:3-total-13, fullness:5-total-11 */
	out[5] = (uint8_t)(((size >> 10) & 0x07) | 0xf8);
	/** fullnes:6-total-11, num:2 */
	out[6] = 0x3f;
	memcpy(out+7, data, size);
	return size + 7;
	#endif
	int profile = 2; // AAC LC
	int freqIdx = 4; // 44.1KHz
	int chanCfg = 2; // CPE

	// fill in ADTS data
	out[0] = (char) 0xFF;
	out[1] = (char) 0xF9;
	out[2] = (char) (((profile - 1) << 6) + (freqIdx << 2) + (chanCfg >> 2));
	out[3] = (char) (((chanCfg & 3) << 6) + (size >> 11));
	out[4] = (char) ((size & 0x7FF) >> 3);
	out[5] = (char) (((size & 7) << 5) + 0x1F);
	out[6] = (char) 0xFC;

	return 7;
}

static inline bool reset_semaphore(struct ff_demux *demux)
{
	os_sem_destroy(demux->demux_sem);
	return os_sem_init(&demux->demux_sem, 0) == 0;
}

static void *demuxing_thread(void *arg)
{
	int ret = 0;
	struct ff_demux *demux = arg;
	if (!demux)
		return NULL;

	AVPacket video_pkt;
	char aac_buf[32] = {};
    int64_t ts_ms = 0, last_ts = 0, sleep_ms = 0;

	av_init_packet(&video_pkt);
	video_pkt.data = NULL;
	video_pkt.size = 0;
	demux->active = true;
	blog(MGW_LOG_INFO, "Start demuxing thread");
	while(/*os_sem_wait(demux->demux_sem) == 0*/demux->active) {
		struct encoder_packet packet = {};
		if (stopping(demux))
			break;
		
		ret = av_read_frame(demux->fmt, &demux->pkt);
		if (ret >= 0) {
			ts_ms = (demux->pkt.dts * av_q2d(demux->fmt->streams[demux->pkt.stream_index]->time_base)) * 1000;
			if (demux->pkt.stream_index == demux->video_index) {
				/** video */
				packet.type = ENCODER_VIDEO;

				/** filter the stream if save to file */
				// fwrite(demux->pkt.data, demux->pkt.size, 1, demux->h264_file);
				if (demux->video_filter_ctx) {
					if (0 == av_bsf_send_packet(demux->video_filter_ctx, &demux->pkt)) {
						//av_bsf_flush(demux->video_filter_ctx);
						while (0 == av_bsf_receive_packet(demux->video_filter_ctx, &video_pkt)) {
							if (demux->h264_file) {
								fwrite(video_pkt.data, video_pkt.size, 1, demux->h264_file);
								fflush(demux->h264_file);
							}
							blog(MGW_LOG_INFO, "write video data! size = %d, data[0]:%02x, data[1]:%02x, data[2]:%02x, data[3]:%02x, data[4]:%02x",
								video_pkt.size, video_pkt.data[0], video_pkt.data[1], video_pkt.data[2], video_pkt.data[3], video_pkt.data[4]);
							packet.keyframe = mgw_avc_keyframe(video_pkt.data, video_pkt.size);
							packet.size = video_pkt.size;
							packet.pts = ts_ms * 1000;
							packet.dts = ts_ms * 1000;
							packet.data = video_pkt.data;

							//demux->proc_packet(demux->param, &packet);

							sleep_ms = packet.pts - last_ts;
							if (sleep_ms < 0)
								sleep_ms = 0;
							last_ts = packet.pts;
							usleep(sleep_ms);
						}
					}
					av_packet_unref(&video_pkt);
				}
			} else if (demux->pkt.stream_index == demux->audio_index) {
				/** audio */
				packet.type = ENCODER_AUDIO;
				packet.size = demux->pkt.size;
				packet.pts = ts_ms * 1000;
				packet.dts = ts_ms * 1000;
				packet.data = demux->pkt.data;

				demux->proc_packet(demux->param, &packet);

				sleep_ms = packet.pts - last_ts;
				if (sleep_ms < 0)
					sleep_ms = 0;
				last_ts = packet.pts;
				usleep(sleep_ms);

				/** filter the stream if save to file */
				if (demux->aac_file) {
					size_t out_size = aac_add_adts_header(demux->pkt.size, aac_buf);
					fwrite(aac_buf, out_size, 1, demux->aac_file);
					fwrite(packet.data, packet.size, 1, demux->aac_file);
				}

				av_packet_unref(&demux->pkt);
			}
		} else if (AVERROR_EOF == ret && demux->cycle_demux) {
			av_seek_frame(demux->fmt, demux->video_index, 0, AVSEEK_FLAG_BACKWARD);
		} else if (ret < 0) {
			blog(MGW_LOG_ERROR, "demuxing file error");
			break;
		}
	}

	if (!stopping(demux));
		pthread_detach(demux->demux_thread);

	blog(MGW_LOG_INFO, "Stop demuxing thread");
	os_atomic_set_bool(&demux->active, false);
	return NULL;
}

bool ff_demux_start(void *data, void (*proc_packet)(void *param, struct encoder_packet *packet), void *param)
{
	struct ff_demux *demux = data;
	if (!data || !proc_packet)
		return false;

	demux->proc_packet = proc_packet;
	demux->param = param;
    reset_semaphore(demux);
	os_atomic_set_bool(&demux->active, true);
	if (0 != pthread_create(&demux->demux_thread, NULL, demuxing_thread, demux)) {
		blog(MGW_LOG_ERROR, "Tried to create demuxing thread failed!");
		return false;
	}
	return true;
}

void ff_demux_stop(void *data)
{
	struct ff_demux *demux = data;
	if (!demux)
		return;

	os_event_signal(demux->demux_stopping);
	if (demux->active)
		pthread_join(demux->demux_thread, NULL);
}

void ff_demux_destroy(void *data)
{
	struct ff_demux *demux = data;
	if (!demux)
		return;
	
	if (!stopping(demux)) {
		ff_demux_stop(demux);
	}

    dstr_free(&demux->src_file);

    avformat_close_input(&demux->fmt);
	av_bsf_free(&demux->video_filter_ctx);
    if (demux->h264_file)
        fclose(demux->h264_file);
    if (demux->aac_file)
        fclose(demux->aac_file);

	os_sem_destroy(demux->demux_sem);
	os_event_destroy(demux->demux_stopping);

	bfree(demux);
}