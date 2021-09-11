#include "mgw-internal.h"
#include "mgw-sources.h"
#include "util/bmem.h"
#include "util/dstr.h"
#include "util/base.h"
#include "util/tlog.h"
#include "util/threading.h"

#include "thirdparty/mgw-libsrt.h"

#define SRT_SOURCE_NAME		"srt-source"

struct srt_source {

};


const char *srt_source_get_name(void *type)
{
	UNUSED_PARAMETER(type);
	return SRT_SOURCE_NAME;
}

void *srt_source_create(mgw_data_t *setting, mgw_source_t *source)
{

}

void srt_source_destroy(void *data)
{

}

bool srt_source_start(void *data)
{

}

void srt_source_stop(void *data)
{

}

mgw_data_t *srt_source_get_defaults(void)
{

}

void srt_source_update(void *data, mgw_data_t *settings)
{

}

mgw_data_t *srt_source_get_settings(void *data)
{

}

size_t srt_source_get_header(void *data, enum encoder_type type, uint8_t **header)
{

}

struct mgw_source_info srt_source_info = {
    .id                 = "srt_source",
    .output_flags       = MGW_SOURCE_AV |
						  MGW_SOUTCE_NETSTREAM,
    .get_name           = srt_source_get_name,
    .create             = srt_source_create,
    .destroy            = srt_source_destroy,
    .start              = srt_source_start,
    .stop               = srt_source_stop,

    .get_defaults       = srt_source_get_defaults,
    .update             = srt_source_update,
    .get_settings       = srt_source_get_settings,
	.get_extra_data		= srt_source_get_header
};