#include "mgw-internal.h"
#include "mgw-sources.h"
#include "util/bmem.h"

#define MGW_FFMPEG_SOURCE_NAME      "ffmpeg_source"

struct ffmpeg_source {
    mgw_source_t        *source;


};


static const char *ffmpeg_source_get_name(void *type)
{
    UNUSED_PARAMETER(type);
    return MGW_FFMPEG_SOURCE_NAME;
}

static void *ffmpeg_source_create(mgw_data_t *setting, mgw_source_t *source)
{
    struct ffmpeg_source *s = bzalloc(sizeof(struct ffmpeg_source));
    s->source = source;



    return s;
}

static void ffmpeg_source_destroy(void *data)
{

}

static bool ffmpeg_source_start(void *data)
{

}

static void ffmpeg_source_stop(void *data)
{

}

static void ffmpeg_source_get_defaults(mgw_data_t *settings)
{

}

static void ffmpeg_source_update(void *data, mgw_data_t *settings)
{

}

static mgw_data_t *ffmpeg_source_get_settings(void *data)
{

}


struct mgw_source_info ffmpeg_source_info = {
    .id                 = "ffmpeg_source",
    .output_flags       = MGW_SOURCE_AV | MGW_SOUTCE_NETSTREAM,
    .get_name           = ffmpeg_source_get_name,
    .create             = ffmpeg_source_create,
    .destroy            = ffmpeg_source_destroy,
    .start              = ffmpeg_source_start,
    .stop               = ffmpeg_source_stop,

    .get_defaults       = ffmpeg_source_get_defaults,
    .update             = ffmpeg_source_update,
    .get_settings       = ffmpeg_source_get_settings
};