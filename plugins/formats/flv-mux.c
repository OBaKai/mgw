#include "util/serializer.h"
#include "util/array-serializer.h"
#include "util/bmem.h"
#include "flv-mux.h"
#include "librtmp/rtmp-helpers.h"

/* TODO: FIXME: this is currently hard-coded to h264 and aac!  ..not that we'll
 * use anything else for a long time. */

//#define DEBUG_TIMESTAMPS
//#define WRITE_FLV_HEADER
#define SEND_VIDEO
#define SEND_AUDIO
#define VIDEO_HEADER_SIZE 5
#define FLV_INFO_SIZE_OFFSET 42
#define MILLISECOND_DEN   1000


int32_t get_ms_time(struct encoder_packet *packet, uint64_t val)
{
	return (int32_t)(val * MILLISECOND_DEN / packet->timebase_den);
}

void write_file_info(FILE *file, int64_t duration_ms, int64_t size)
{
	char buf[64];
	char *enc = buf;
	char *end = enc + sizeof(buf);

	fseek(file, FLV_INFO_SIZE_OFFSET, SEEK_SET);

	enc_num_val(&enc, end, "duration", (double)duration_ms / 1000.0);
	enc_num_val(&enc, end, "fileSize", (double)size);

	fwrite(buf, 1, enc - buf, file);
}

static bool build_flv_meta_data(mgw_data_t *settings,
		uint8_t **output, size_t *size, size_t a_idx)
{
	char buf[4096];
	char *enc = buf;
	int object_cnt = 3;
	char *end = enc+sizeof(buf);
#ifdef SEND_VIDEO
	object_cnt += 5;
#endif
#ifdef SEND_AUDIO
	object_cnt += 6;
#endif

	enc_str(&enc, end, "onMetaData");

	*enc++ = AMF_ECMA_ARRAY;
    enc    = AMF_EncodeInt32(enc, end, object_cnt);

	enc_num_val(&enc, end, "duration", 0.0);
	enc_num_val(&enc, end, "fileSize", 0.0);
#ifdef SEND_VIDEO
	enc_str_val(&enc, end, "videocodecid",  	"avc1");
    enc_num_val(&enc, end, "width",     		mgw_data_get_int(settings, "width"));
    enc_num_val(&enc, end, "height",    		mgw_data_get_int(settings, "height"));
    enc_num_val(&enc, end, "videodatarate", 	mgw_data_get_int(settings, "vbps") * 1000);	//kbps
    enc_num_val(&enc, end, "framerate",     	mgw_data_get_int(settings, "fps"));
#endif
#ifdef SEND_AUDIO
	long long channels = mgw_data_get_int(settings, "channels");
	enc_str_val(&enc, end, "audiocodecid",      "mp4a");
	enc_num_val(&enc, end, "audiodatarate",     mgw_data_get_int(settings, "abps"));  //kbps
    enc_num_val(&enc, end, "audiosamplerate",   mgw_data_get_int(settings, "samplerate"));
	enc_num_val(&enc, end, "audiosamplesize",   mgw_data_get_int(settings, "samplesize"));
    enc_num_val(&enc, end, "audiochannels",     channels);
	if(SPEAKERS_MONO == channels)
    	enc_bool_val(&enc, end, "stereo", false);
	else if (SPEAKERS_STEREO == channels)
		enc_bool_val(&enc, end, "stereo", true);
#endif	
	enc_str_val(&enc, end, "encoder", "ucast rtmp module");
	
	*enc++  = 0;
	*enc++  = 0;
	*enc++  = AMF_OBJECT_END;

	*size   = enc-buf;
    *output = (uint8_t *)bmemdup(buf, *size);
	mgw_data_release(settings);
	return true;
}

bool flv_meta_data(mgw_data_t *settings, uint8_t **output, size_t *size,
		bool write_header, size_t audio_idx)
{
	struct array_output_data data;
	struct serializer s;
	uint8_t *meta_data = NULL;
	size_t  meta_data_size;
	uint32_t start_pos;

	array_output_serializer_init(&s, &data);

	if (!build_flv_meta_data(settings, &meta_data, &meta_data_size,
				audio_idx)) {
        bfree(meta_data);
		return false;
	}

	if (write_header) {
		s_write(&s, "FLV", 3);
		s_w8(&s, 1);
		s_w8(&s, 5);
		s_wb32(&s, 9);
		s_wb32(&s, 0);
	}

	start_pos = serializer_get_pos(&s);

	s_w8(&s, RTMP_PACKET_TYPE_INFO);

	s_wb24(&s, (uint32_t)meta_data_size);
	s_wb32(&s, 0);
	s_wb24(&s, 0);

	s_write(&s, meta_data, meta_data_size);

	s_wb32(&s, (uint32_t)serializer_get_pos(&s) - start_pos - 1);

	*output = data.bytes.array;
	*size   = data.bytes.num;

    bfree(meta_data);
	return true;
}

static void flv_video(struct serializer *s, int32_t dts_offset,
		struct encoder_packet *packet, bool is_header)
{
	//int64_t offset  = packet->pts - packet->dts;
	int32_t time_ms;// = get_ms_time(packet, packet->dts) - dts_offset;
	if (!packet->data || !packet->size)
		return;

	s_w8(s, RTMP_PACKET_TYPE_VIDEO);

	time_ms = packet->pts;
	s_wb24(s, (uint32_t)packet->size + 5);
	s_wb24(s, time_ms);
	s_w8(s, (time_ms >> 24) & 0x7F);
	s_wb24(s, 0);

	/* these are the 5 extra bytes mentioned above */
	s_w8(s, packet->keyframe ? 0x17 : 0x27);
	s_w8(s, is_header ? 0 : 1);
	s_wb24(s, 0/*get_ms_time(packet, offset)*/);
	s_write(s, packet->data, packet->size);

	/* write tag size (starting byte doesn't count) */
	s_wb32(s, (uint32_t)serializer_get_pos(s) - 1);
}

static void flv_audio(struct serializer *s, int32_t dts_offset,
		struct encoder_packet *packet, bool is_header)
{
	int32_t time_ms;// = get_ms_time(packet, packet->dts) - dts_offset;
	int extra_byte = is_header ? 0 : 2;
	if (!packet->data || !packet->size)
		return;
	
	time_ms = packet->pts;
	s_w8(s, RTMP_PACKET_TYPE_AUDIO);

	s_wb24(s, (uint32_t)packet->size + extra_byte);
	s_wb24(s, time_ms);
	s_w8(s, (time_ms >> 24) & 0x7F);
	s_wb24(s, 0);

	/* these are the two extra bytes mentioned above */
	if (!is_header) {
		s_w8(s, 0xaf);
		s_w8(s, /*is_header ? 0 : */1);
	}
	s_write(s, packet->data, packet->size);

	/* write tag size (starting byte doesn't count) */
	s_wb32(s, (uint32_t)serializer_get_pos(s) - 1);
}

void flv_packet_mux(struct encoder_packet *packet, int32_t dts_offset,
		uint8_t **output, size_t *size, bool is_header)
{
	struct array_output_data data;
	struct serializer s;

	array_output_serializer_init(&s, &data);

	if (packet->type == ENCODER_VIDEO)
		flv_video(&s, dts_offset, packet, is_header);
	else
		flv_audio(&s, dts_offset, packet, is_header);

	*output = data.bytes.array;
	*size   = data.bytes.num;
}

#if 0
static inline bool has_start_code(const uint8_t *data)
{
    if (data[0] != 0 || data[1] != 0)
           return false;

    return data[2] == 1 || (data[2] == 0 && data[3] == 1);
}

/size_t parse_avc_header(const media_meta_t *mm, uint8_t **header)
{
    struct array_output_data output;
    struct serializer s;
    array_output_serializer_init(&s, &output);

	/* here are AVCDecoderConfigurationRecord */

    s_w8(&s, 0x01);
    s_write(&s, mm->sps.array+1, 3);
    s_w8(&s, 0xff);
    s_w8(&s, 0xe1);

    s_wb16(&s, (uint16_t)mm->sps.len);
    s_write(&s, mm->sps.array, mm->sps.len);
    s_w8(&s, 0x01);
    s_wb16(&s, (uint16_t)mm->pps.len);
    s_write(&s, mm->pps.array, mm->pps.len);
    *header = output.bytes.array;
    return output.bytes.num;
}
#endif
