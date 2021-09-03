#ifndef _CODEC_DEF_H_
#define _CODEC_DEF_H_

#include "bmem.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PLANES  8
#define MGW_MAX_PACKET_SIZE		512000	//500 KB
#define MGW_AVCC_HEADER_SIZE	4

#define FRAME_PRIORITY_LOW      1
#define FRAME_PRIORITY_HIGH     2

#define FRAME_CONSUME_SLOW		-1
#define FRAME_CONSUME_FAST		-2
#define FRAME_CONSUME_PERR		-3

enum encoder_type {
    ENCODER_AUDIO,
    ENCODER_VIDEO
};

typedef enum encoder_id {
    ENCID_NONE = 0,
    ENCID_AAC,
    ENCID_H264,
    ENCID_HEVC,
}encoder_id_t;

typedef enum frame_type {
    FRAME_UNKNOWN = -1,
    FRAME_I = 0,
    FRAME_B,
    FRAME_P,
    FRAME_IDR,
    FRAME_SEI,
    FRAME_SPS,
    FRAME_PPS,
    FRAME_AAC = 7,
    FRAM_VPS,
}frame_t;

enum audio_obj_type {
    AUDIO_NULL = 0,
    AUDIO_AAC_Main,
    AUDIO_AAC_LC,
    AUDIO_AAC_SSR,
    AUDIO_AAC_LTP,
    AUDIO_SBR,
    AUDIO_AAC_Scalable,
};

typedef enum video_format {
	VIDEO_FORMAT_NONE,

	/* planar 420 format */
	VIDEO_FORMAT_I420, /* three-plane */
	VIDEO_FORMAT_NV12, /* two-plane, luma and packed chroma */

	/* packed 422 formats */
	VIDEO_FORMAT_YVYU,
	VIDEO_FORMAT_YUY2, /* YUYV */
	VIDEO_FORMAT_UYVY,

	/* packed uncompressed formats */
	VIDEO_FORMAT_RGBA,
	VIDEO_FORMAT_BGRA,
	VIDEO_FORMAT_BGRX,
	VIDEO_FORMAT_Y800, /* grayscale */

	/* planar 4:4:4 */
	VIDEO_FORMAT_I444,
}video_fmt;

enum speaker_layout {
	SPEAKERS_UNKNOWN,   /**< Unknown setting, fallback is stereo. */
	SPEAKERS_MONO,      /**< Channels: MONO */
	SPEAKERS_STEREO,    /**< Channels: FL, FR */
	SPEAKERS_2POINT1,   /**< Channels: FL, FR, LFE */
	SPEAKERS_4POINT0,   /**< Channels: FL, FR, FC, RC */
	SPEAKERS_4POINT1,   /**< Channels: FL, FR, FC, LFE, RC */
	SPEAKERS_5POINT1,   /**< Channels: FL, FR, FC, LFE, RL, RR */
	SPEAKERS_7POINT1=8, /**< Channels: FL, FR, FC, LFE, RL, RR, SL, SR */
};

enum audio_format {
	AUDIO_FORMAT_UNKNOWN,

	AUDIO_FORMAT_U8BIT,
	AUDIO_FORMAT_16BIT,
	AUDIO_FORMAT_32BIT,
	AUDIO_FORMAT_FLOAT,

	AUDIO_FORMAT_U8BIT_PLANAR,
	AUDIO_FORMAT_16BIT_PLANAR,
	AUDIO_FORMAT_32BIT_PLANAR,
	AUDIO_FORMAT_FLOAT_PLANAR,
};

struct video_config {
    uint16_t    width;
    uint16_t    height;
    uint32_t    fps;
};

struct audio_config {
    uint16_t    channels;
    uint16_t    sample_size;
    uint32_t    sample_rate;
};

struct encoder_config {
    enum encoder_id     id;
    uint32_t            bps;
    union {
        struct video_config video;
        struct audio_config audio;
    };
};

/** Encoder output packet */
struct encoder_packet {
    uint8_t               *data;        /**< Packet data */
    size_t                size;         /**< Packet size */

    int64_t               pts;          /**< Presentation timestamp */
    int64_t               dts;          /**< Decode timestamp */

    int32_t               timebase_num; /**< Timebase numerator */
    int32_t               timebase_den; /**< Timebase denominator */

    enum encoder_type     type;         /**< Encoder type */

    bool                  keyframe;     /**< Is a keyframe */
    int64_t               dts_usec;

    /* System DTS in microseconds */
    int64_t               sys_dts_usec;
    int                   priority;
    int                   drop_priority;

    /** Audio track index (used with outputs) */
    size_t                track_idx;

    /** Encoder from which the track originated from */
//    obs_encoder_t         *encoder;
    volatile long         refs;
};
typedef struct encoder_packet encoder_packet_t;

struct raw_video_frame {
    uint8_t             *data[MAX_PLANES];
    uint32_t            linesize[MAX_PLANES];
    uint32_t            width;
    uint32_t            height;
    uint64_t            timestamp;

    enum video_format   format;
};

struct raw_audio_frame {
	const uint8_t       *data[MAX_PLANES];
	uint32_t            frames;

	enum speaker_layout speakers;
	enum audio_format   format;
	uint32_t            samples_per_sec;

	uint64_t            timestamp;
};

int8_t mgw_avc_get_startcode_len(const uint8_t *data);
bool mgw_avc_keyframe(const uint8_t *data, size_t size);
const uint8_t *mgw_avc_find_startcode(const uint8_t *p, const uint8_t *end);

size_t mgw_avc_get_sps(const uint8_t *data, size_t size, uint8_t **sps);
size_t mgw_avc_get_pps(const uint8_t *data, size_t size, uint8_t **pps);
size_t mgw_avc_get_keyframe(const uint8_t *data, size_t size, uint8_t **keyframe);

bool mgw_avc_avcc2annexb(struct encoder_packet *avcc_pkt, struct encoder_packet *annexb_pkt);
bool mgw_avc_annexb2avcc(struct encoder_packet *annexb_pkt, struct encoder_packet *avcc_pkt);

size_t mgw_parse_avc_header(uint8_t **header, uint8_t *data, size_t size);
size_t mgw_parse_hevc_header(uint8_t **header, uint8_t *data, size_t size);

size_t mgw_get_aac_lc_header(
			uint8_t channels, uint8_t samplesize,
			uint32_t samplerate, uint8_t **header);
size_t mgw_get_aaclc_flv_header(
			uint8_t channels, uint8_t samplesize,
			uint32_t samplerate, uint8_t **header);
size_t mgw_aac_add_adts(uint32_t samplerate, int profile,
		uint32_t channels, size_t size, uint8_t *data, uint8_t *out);
size_t mgw_aac_leave_adts(uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size);

const char *mgw_get_vcodec_id(encoder_id_t id);

#ifdef __cplusplus
}
#endif
#endif