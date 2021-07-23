#include "codec-def.h"
#include "array-serializer.h"
#include "base.h"

static uint8_t get_samplerate_index(uint32_t sampleRate)
{
    uint8_t sampleRateIndex = 15;
    if (sampleRate > 0) {
        switch(sampleRate) {
            case 96000: sampleRateIndex = 0; break;
            case 88200: sampleRateIndex = 1; break;
            case 64000: sampleRateIndex = 2; break;
            case 48000: sampleRateIndex = 3; break;
            case 44100: sampleRateIndex = 4; break;
            case 32000: sampleRateIndex = 5; break;
            case 24000: sampleRateIndex = 6; break;
            case 22050: sampleRateIndex = 7; break;
            case 16000: sampleRateIndex = 8; break;
            case 12000: sampleRateIndex = 9; break;
            case 11025: sampleRateIndex = 10; break;
            case 8000 : sampleRateIndex = 11; break;
            case 7350 : sampleRateIndex = 12; break;
            default   : sampleRateIndex = 15; break;
        }
    }
    return sampleRateIndex;
}

/** ISO/IEC 14496-3 1.6.2 Syntax (AAC-LC configuration) AudioSpecificConfig */
size_t mgw_get_aac_lc_header(
			uint8_t channels, uint8_t samplesize,
			uint32_t samplerate, uint8_t **header)
{
	int i = 0;
	uint8_t buf[32] = {};
	uint8_t samplingFrequencyIndex = get_samplerate_index(samplerate);

	buf[i] = (AUDIO_AAC_LC << 3) & 0xf8;
	buf[i++] |= (samplingFrequencyIndex >> 1) & 0x07;
	buf[i] = (samplingFrequencyIndex << 7) & 0x80;
	if (0xf == samplingFrequencyIndex) {
		buf[i++] |= ((uint8_t)(samplerate >> (16 + 1))) & 0x7f;
		buf[i++] = (uint8_t)(samplerate >> (8 + 1));
		buf[i++] = (uint8_t)(samplerate >> 1);
		buf[i] = (((uint8_t)samplerate) << 7) & 0x80;
	}
	buf[i++] |= (((uint8_t)channels) << 3) & 0x78;

	if (!(*header))
		*header = bzalloc(i);

	memcpy(*header, buf, i);
	return i;
}
size_t mgw_get_aaclc_flv_header(
			uint8_t channels, uint8_t samplesize,
			uint32_t samplerate, uint8_t **header)
{
	int i = 0;
	uint8_t buf[32] = {};

	buf[i++] = 0xAF;        // 1010 11 1 1    (AAC, 44.1K, 16bits, stereo)
    buf[i++] = 0x00;        //aac sequence header: 0x00, aac raw data:0x01
	uint8_t *temp_header = buf + i;
	i += mgw_get_aac_lc_header(channels, samplesize, samplerate, &temp_header);

	if (samplesize == 8)
		buf[0] &= 0xfd;
	if (channels == 1)
		buf[0] &=0xfe;

	if (!(*header))
		*header = bzalloc(i);

	memcpy(*header, buf, i);
	return i;
}

/** ISO/IEC 14496-15:2017  5.3.3.1.2 Syntax */
/** 
aligned(8) class AVCDecorderConfigurationRecord {
	unsigned int(8) configurationVersion = 1;
	unsigned int(8) AVCProfileIndication;
	unsinged int(8) profile_compatibility;
	unsigned int(8) AVCLevelIndication;
	bit(6) reserved = '111111'b;
	uinsigned int(2) lengthSizeMinusOne;
	bit(3) reserved = '111'b;
	unsigned int(5) numOfSequenceParameterSets;
	for (i = 0; i < numOfSequenceParameterSets; i++) {
			unsigned int(16) sequenceParameterSetLength;
			bit(8*sequenceParameterSetLength) sequenceParameterSetNALUnit;
	}
	unsigned int (8) numOfPictureParameterSets;
	for (i = 0; i < numOfPictureParameterSets; i++) {
			unsigned int(16) pictureParameterSetLength;
			bit(8*pictureParameterSetLength) pictureParameterSetNALUnit;
	}
	if (AVCProfileIndication == 100 || AVCProfileIndication == 110 ||
		AVCProfileIndication == 122 || AVCProfileIndication == 144)
	{
		bit(6) reserved = '111111'b;
		unsigned int(2) chroma_format;
		bit(5) reserved = '11111'b;
		unsigned int(3) bit_depth_luma_minus8;
		bit(5) reserved = '11111'b;
		unsigned int(3) bit_depth_chroma_minus8;
		unsigned int(8) numOfSequenceParameterSetExt;
		for (i = 0;, i < numOfSequenceParameterSetExt; i++) {
			unsigned int(16) sequenceParameterSetExtLength;
			bit(8*sequenceParameterSetExtLength) sequenceParameterSetExtNALUnit;
		}
	}
}
 */


/* NOTE: I noticed that FFmpeg does some unusual special handling of certain
 * scenarios that I was unaware of, so instead of just searching for {0, 0, 1}
 * we'll just use the code from FFmpeg - http://www.ffmpeg.org/ */
static const uint8_t *ff_avc_find_startcode_internal(const uint8_t *p,
		const uint8_t *end)
{
	const uint8_t *a = p + 4 - ((intptr_t)p & 3);

	for (end -= 3; p < a && p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	for (end -= 3; p < end; p += 4) {
		uint32_t x = *(const uint32_t*)p;

		if ((x - 0x01010101) & (~x) & 0x80808080) {
			if (p[1] == 0) {
				if (p[0] == 0 && p[2] == 1)
					return p;
				if (p[2] == 0 && p[3] == 1)
					return p+1;
			}

			if (p[3] == 0) {
				if (p[2] == 0 && p[4] == 1)
					return p+2;
				if (p[4] == 0 && p[5] == 1)
					return p+3;
			}
		}
	}

	for (end += 3; p < end; p++) {
		if (p[0] == 0 && p[1] == 0 && p[2] == 1)
			return p;
	}

	return end + 3;
}

const uint8_t *mgw_avc_find_startcode(const uint8_t *p, const uint8_t *end)
{
	const uint8_t *out = ff_avc_find_startcode_internal(p, end);
	if (p < out && out < end && !out[-1]) out--;
	return out;
}

bool mgw_avc_keyframe(const uint8_t *data, size_t size)
{
	const uint8_t *nal_start, *nal_end;
	const uint8_t *end = data + size;
	int type;

	nal_start = mgw_avc_find_startcode(data, end);
	while (true) {
		while (nal_start < end && !*(nal_start++));

		if (nal_start == end)
			break;

		type = nal_start[0] & 0x1F;

		if (type == 0x5 || type == 0x1)
			return (type == 0x5);

		nal_end = mgw_avc_find_startcode(nal_start, end);
		nal_start = nal_end;
	}

	return false;
}

static inline bool has_start_code(const uint8_t *data)
{
	if (data[0] != 0 || data[1] != 0)
	       return false;

	return data[2] == 1 || (data[2] == 0 && data[3] == 1);
}

int8_t mgw_avc_get_startcode_len(const uint8_t *data)
{
	if (!data || data[0] || data[1])
		return -1;

	if (data[2] == 1)
		return 3;
	else if (!data[2] && data[3] == 1)
		return 4;
	else
		return -1;
}

static size_t mgw_avc_get_nal(const uint8_t *data, size_t size,
					uint8_t **nal, uint8_t get_type, size_t max_check)
{
	size_t nal_size = 0, check_len;
	int8_t start_code_len = 0;
	const uint8_t *p = data;

	if (!data || !size || !nal)
		return 0;

	check_len = size > max_check ? max_check : size;
	*nal = NULL;

	for (int i = 0; i < check_len; i++) {
		if ((start_code_len = mgw_avc_get_startcode_len(++p)) < 0) {
			continue;
		}
		p += start_code_len;

		if (*nal) {
			nal_size = p - start_code_len - *nal;
			break;
		}

		if (get_type == ((*p) & 0x1f)) {
			*nal = (uint8_t *)p;

			if (0x5 == get_type) {
				nal_size = size - (p - data);
				break;
			}
		}
	}
	return nal_size;
}

size_t mgw_avc_get_sps(const uint8_t *data, size_t size, uint8_t **sps)
{
	return mgw_avc_get_nal(data, size, sps, 0x7, 64);
}

size_t mgw_avc_get_pps(const uint8_t *data, size_t size, uint8_t **pps)
{
	return mgw_avc_get_nal(data, size, pps, 0x8, 32);
}

size_t mgw_avc_get_keyframe(const uint8_t *data, size_t size, uint8_t **keyframe)
{
	return mgw_avc_get_nal(data, size, keyframe, 0x5, size);
}

static void get_avc_sps_pps(const uint8_t *data, size_t size,
		const uint8_t **sps, size_t *sps_size,
		const uint8_t **pps, size_t *pps_size)
{
	const uint8_t *nal_start, *nal_end;
	const uint8_t *end = data+size;
	int type;

	nal_start = mgw_avc_find_startcode(data, end);
	while (true) {
		while (nal_start < end && !*(nal_start++));

		if (nal_start == end)
			break;

		nal_end = mgw_avc_find_startcode(nal_start, end);

		type = nal_start[0] & 0x1F;
		if (type == 0x7) {
			*sps = nal_start;
			*sps_size = nal_end - nal_start;
		} else if (type == 0x8) {
			*pps = nal_start;
			*pps_size = nal_end - nal_start;
		}

		nal_start = nal_end;
	}
}

static void get_hevc_vps_sps_pps(const uint8_t *data, size_t size,
        const uint8_t **vps, size_t vps_size,
        const uint8_t **sps, size_t sps_size,
        const uint8_t **pps, size_t pps_size)
{

}

/** ISO/IEC 14496-15:2017  5.3.3.1.2 Syntax AVCDecorderConfigurationRecord */
size_t mgw_parse_avc_header(uint8_t **header, uint8_t *data, size_t size)
{
	struct array_output_data output;
    struct serializer s;
	const uint8_t *sps = NULL, *pps = NULL;
	size_t sps_size = 0, pps_size = 0;

    array_output_serializer_init(&s, &output);
    /** find and leave sps,pps start code */
	get_avc_sps_pps(data, size, &sps, &sps_size, &pps, &pps_size);
	if (!sps || !pps || sps_size < 4)
		return 0;

    s_w8(&s, 0x01);
    s_write(&s, sps+1, 3);
    s_w8(&s, 0xff);
    s_w8(&s, 0xe1);

    s_wb16(&s, (uint16_t)sps_size);
    s_write(&s, sps, sps_size);
    s_w8(&s, 0x01);
    s_wb16(&s, (uint16_t)pps_size);
    s_write(&s, pps, pps_size);
    *header = output.bytes.array;
    return output.bytes.num;
}

/** ISO/IEC 14496-15:2017  8.3.3.1.2 Syntax  page 79 HEVCDecorderConfigurationRecord */
size_t mgw_parse_hevc_header(uint8_t **header, uint8_t *data, size_t size)
{
	size_t header_size = 0;


	return header_size;
}