#include <signal.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include "mgw-internal.h"
#include "mgw-outputs.h"

#include "util/base.h"
#include "util/dstr.h"
#include "util/platform.h"

#undef  SRT_MODULE_NAME
#define SRT_MODULE_NAME     "srt_stream"

struct srt_stream {
	uint64_t			total_sent_bytes;

	mgw_output_t		*output;
};


static const char *srt_stream_get_name(void *type)
{
	UNUSED_PARAMETER(type);
	return SRT_MODULE_NAME;
}

static void *srt_stream_create(mgw_data_t *setting, mgw_output_t *output)
{
	if (!setting || !output)
		return NULL;

	struct srt_stream *stream = bzalloc(sizeof(struct srt_stream));
	stream->output = output;

	return stream;
}

static void srt_stream_destroy(void *data)
{
	struct srt_stream *stream = data;
	if (!stream)
		return;
}

static bool srt_stream_start(void *data)
{
	struct srt_stream *stream = data;
	if (!stream)
		return false;

	return true;
}

static void srt_stream_stop(void *data)
{
	struct srt_stream *stream = data;
	if (!stream)
		return;


}

static uint64_t srt_stream_get_total_bytes(void *data)
{
	struct srt_stream *stream = data;
	if (!stream)
		return 0;
	return stream->total_sent_bytes;
}

static mgw_data_t *srt_stream_get_default(void)
{
	mgw_data_t *settings = mgw_data_create();

	return settings;
}

static mgw_data_t *srt_stream_get_settings(void *data)
{
	struct srt_stream *stream = data;
	if (!stream)
		return NULL;

	mgw_data_t *settings = mgw_data_create();

	return settings;
}

static void srt_stream_apply_update(void *data, mgw_data_t *settings)
{

}

struct mgw_output_info srt_output_info = {
	.id                 = "srt_output",
	.flags              = MGW_OUTPUT_AV |
							MGW_OUTPUT_ENCODED,
	.get_name           = srt_stream_get_name,
	.create             = srt_stream_create,
	.destroy            = srt_stream_destroy,
	.start              = srt_stream_start,
	.stop               = srt_stream_stop,
	.get_total_bytes    = srt_stream_get_total_bytes,

	.get_default        = srt_stream_get_default,
	.get_settings       = srt_stream_get_settings,
	.update             = srt_stream_apply_update
};