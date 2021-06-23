
#ifndef _FORMATS_FLV_MUX_H_
#define _FORMATS_FLV_MUX_H_

#include "util/codec-def.h"
#include "util/mgw-data.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t get_ms_time(struct encoder_packet *packet, uint64_t val);
void write_file_info(FILE *file, int64_t duration_ms, int64_t size);

bool flv_meta_data(mgw_data_t *settings, uint8_t **output, size_t *size,
		bool write_header, size_t audio_idx);
void flv_packet_mux(struct encoder_packet *packet, int32_t dts_offset,
		uint8_t **output, size_t *size, bool is_header);

#ifdef __cplusplus
}
#endif
#endif