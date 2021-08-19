#include "mgw-internal.h"
#include "mgw-sources.h"
#include "util/bmem.h"
#include "util/dstr.h"
#include "util/base.h"
#include "util/threading.h"

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libswscale/swscale.h"

#define MGW_FFMPEG_SOURCE_NAME      "ffmpeg_source"

static pthread_once_t ff_source_context_once = PTHREAD_ONCE_INIT;

struct ffmpeg_source {
    mgw_source_t        *source;

	volatile bool		actived;
	os_event_t			*stop_event;
	pthread_t			receive_thread;

	mgw_data_t			*setting;
	struct dstr			uri;

	bool				is_local_file;
	bool				is_looping;
	int					sws_width, sws_height;
	AVFormatContext		*fmt_ctx;
	AVStream			*vst, *ast;
	AVBSFContext		*video_filter_ctx;
	struct SwsContext	*sws_ctx;
};

static inline bool stopping(struct ffmpeg_source *source)
{
	return os_event_try(source->stop_event) != EAGAIN;
}
 
static inline bool actived(struct ffmpeg_source *source)
{
	return os_atomic_load_bool(&source->actived);
}

/**< Initialize ffmpeg all environment*/
static void init_once_thread(void)
{
	avformat_network_init();
}

static const char *ffmpeg_source_get_name(void *type)
{
    UNUSED_PARAMETER(type);
    return MGW_FFMPEG_SOURCE_NAME;
}

static void ffmpeg_source_destroy(void *data)
{

}

static void *ffmpeg_source_create(mgw_data_t *setting, mgw_source_t *source)
{
	if (!setting || !source)
		return NULL;

    struct ffmpeg_source *s = bzalloc(sizeof(struct ffmpeg_source));
    s->source = source;
	s->setting = setting;

	if (0 != os_event_init(&s->stop_event, OS_EVENT_TYPE_MANUAL))
		goto error;

	if (0 != pthread_once(&ff_source_context_once, init_once_thread)) {
		blog(MGW_LOG_ERROR, "Create once thread to initialize ffmpeg failed!");
		goto error;
	}

	return s;

error:
	ffmpeg_source_destroy(s);
    return NULL;
}

static bool ffmpeg_source_start(void *data)
{
    return false;
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
    return NULL;
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