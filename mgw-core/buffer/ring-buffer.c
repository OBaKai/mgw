#include "ring-buffer.h"
#include "stream_buff.h"
#include "stream_sort.h"

#include "util/dstr.h"
#include "util/threading.h"
#include "util/base.h"
#include "util/tlog.h"

/** Set ring buffer to 10M Bytes by default */
#define RING_BUFFER_SIZE_DEF        10*1024*1024
/** Set ring buffer to 30 frames by default */
#define RING_BUFFER_CAP_DEF         30
/** Set ring buffer max frame size for getting */
#define RING_BUFFER_MAX_FRAMESIZE   500*1024

struct ring_buffer {
	bool            sort;
	volatile long   refs;
	BuffContext     *bc;
	mgw_data_t      *settings;
	void            *sort_list;

	pthread_mutex_t write_mutex;
};

mgw_data_t *mgw_rb_get_default(void)
{
    mgw_data_t *setting = mgw_data_create();

    mgw_data_set_default_bool(setting, "sort", true);
    mgw_data_set_default_bool(setting, "heap_mem", false);
    mgw_data_set_default_bool(setting, "read_by_time", true);
    mgw_data_set_default_int(setting, "mem_size", RING_BUFFER_SIZE_DEF);
    mgw_data_set_default_int(setting, "capacity", RING_BUFFER_CAP_DEF);

    return setting;
}

static int datacallback(void *puser, sc_sortframe *oframe)
{
	return PutOneFrameToBuff((BuffContext *)puser, (uint8_t*)oframe->frame, \
            oframe->frame_len, oframe->timestamp, oframe->frametype, oframe->priority);
}

void *mgw_rb_create(mgw_data_t *settings, void *source)
{
    struct ring_buffer *rb = bzalloc(sizeof(struct ring_buffer));
    rb->settings = mgw_data_newref(settings);

    if (!settings)
        rb->settings = mgw_rb_get_default();

	pthread_mutex_init(&rb->write_mutex, NULL);

    io_mode_t io;
    const char *io_m = mgw_data_get_string(rb->settings, "io_mode");
    if (0 == strcmp(io_m, "read"))
        io = IO_MODE_READ;
    else if (0 == strcmp(io_m, "write"))
        io = IO_MODE_WRITE;

    rb->sort = mgw_data_get_bool(rb->settings, "sort");
    size_t min_delay = mgw_data_get_int(rb->settings, "min_delay");
    size_t max_delay = mgw_data_get_int(rb->settings, "max_delay");
    const char *name = mgw_data_get_string(rb->settings, "stream_name");
    const char *id = mgw_data_get_string(rb->settings, "info_id");

    rb->bc = CreateStreamBuff(mgw_data_get_int(rb->settings, "mem_size"),
                            name,id,
                            mgw_data_get_int(rb->settings, "capacity"),
                            mgw_data_get_bool(rb->settings, "heap_mem"),
                            io,
                            mgw_data_get_bool(rb->settings, "read_by_time"),
                            (void *)source);
    if (!rb->bc)
        goto error;

    /* as source writer, create the sort list if enable sort */
    if (IO_MODE_WRITE == io && rb->sort) {
        RegisterSortInfo info = {};
        info.Datacallback = datacallback;
        info.puser = rb->bc;
        info.uiSize = SORT_SIZE_DEF;
        if (min_delay > 0)
            info.uiSortTime = min_delay;
        else
            info.uiSortTime = SORT_TIME_DEF;

        if (max_delay > 0)
            info.uiMaxSortTime = max_delay;

        dstr_copy(&info.name, name);
        dstr_copy(&info.userid, id);

        rb->sort_list = pCreateStreamSort(&info);
		dstr_free(&info.name);
		dstr_free(&info.userid);

        if (!rb->sort_list)
            goto error;
    }

    return rb;
error:
    mgw_rb_destroy(rb);
    return NULL;
}

struct source_param *mgw_rb_get_source_param(void *data)
{
    struct ring_buffer *rb = data;
	SmemoryHead *shared_head = NULL;

	if (!rb || !rb->bc->position.pstuHead)
		return NULL;

	shared_head = (SmemoryHead *)rb->bc->position.pstuHead;
    return (struct source_param *)shared_head->priv_data;
}

void mgw_rb_destroy(void *data)
{
	struct ring_buffer *rb = data;

	if (!rb)
		return;

	if (rb->settings)
		mgw_data_release(rb->settings);

	if (!!rb->sort_list)
		DelectStreamSort(rb->sort_list);

	if (rb->bc)
		DeleteStreamBuff(rb->bc);

	pthread_mutex_destroy(&rb->write_mutex);
	bfree(rb);
}

void mgw_rb_addref(void *data)
{
    struct ring_buffer *rb = data;
    if (data)
        os_atomic_inc_long(&rb->refs);
}

void mgw_rb_release(void *data)
{
    struct ring_buffer *rb = data;
    if (data && (os_atomic_dec_long(&rb->refs) == -1))
        mgw_rb_destroy(data);
}

size_t mgw_rb_write_packet(void *data, struct encoder_packet *packet)
{
    struct ring_buffer *rb = data;
    size_t write_size = 0;
    if (!rb || !packet)
        return -1;

    frame_t frame_type;
    if (ENCODER_VIDEO == packet->type) {
        frame_type = packet->keyframe ? FRAME_I : FRAME_P;

		if ((packet->data[0] || packet->data[1] ||
			(packet->data[3] != 1 && packet->data[4] != 1)))
			tlog(TLOG_INFO, "Ring buffer find a video frame "
						"data[0]:%02x, data[1]:%02x, data[2]:%02x, data[3]:%02x, data[3]:%02x",
						packet->data[0], packet->data[1], packet->data[2],packet->data[3],packet->data[4]);
    } else if (ENCODER_AUDIO == packet->type) {
        frame_type = FRAME_AAC;
    }

	pthread_mutex_lock(&rb->write_mutex);
    if (rb->sort) {
        sc_sortframe iframe = {};
		iframe.frametype = frame_type;
		iframe.frame_len = packet->size;
		iframe.timestamp = packet->pts;
		iframe.frame = (char *)packet->data;
        iframe.priority = packet->priority;
		write_size = PutFrameStreamSort(&rb->sort_list, &iframe);
    } else {
        write_size = PutOneFrameToBuff(rb->bc, packet->data, \
                packet->size, packet->pts, frame_type, packet->priority);
    }
	pthread_mutex_unlock(&rb->write_mutex);
    return write_size;
}

int mgw_rb_read_packet(void *data, struct encoder_packet *packet)
{
	struct ring_buffer *rb = data;
    int read_size = 0;
    if (!data || !packet)
        return FRAME_CONSUME_PERR;

    if (IO_MODE_WRITE == rb->bc->mode)
        return FRAME_CONSUME_PERR;
    frame_t frame_type = FRAME_UNKNOWN;
    read_size = GetOneFrameFromBuff(rb->bc, &packet->data, RING_BUFFER_MAX_FRAMESIZE,
            						&packet->pts, &frame_type, &packet->priority);

    if (FRAME_AAC == frame_type)
        packet->type = ENCODER_AUDIO;
    else
        packet->type = ENCODER_VIDEO;

    if (frame_type == FRAME_I || frame_type == FRAME_IDR)
        packet->keyframe = true;
	else
		packet->keyframe = false;

    packet->size = read_size;
    return read_size;
}