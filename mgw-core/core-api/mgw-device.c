#include "mgw.h"
#include "util/tlog.h"

extern struct mgw_core *mgw;

static void mgw_device_destroy(mgw_device_t *device)
{
    if (device->stream_list)
        mgw_stream_release_all(device->stream_list);

    mgw_context_data_free(&device->context);
    pthread_mutex_destroy(&device->stream_mutex);

    bfree(device);
}

mgw_device_t *mgw_device_create(const char *id, const char *name, mgw_data_t *settings)
{
    if ((!id && !name) || !settings) return NULL;

    mgw_device_t *device = bzalloc(sizeof(struct mgw_device));
    if (0 != pthread_mutex_init(&device->stream_mutex, NULL))
        goto error;

    if (!mgw_context_data_init(device, &device->context,
                MGW_OBJ_TYPE_DEVEICE, settings, name, false)) {
        tlog(TLOG_ERROR, "Initialize device %s context failed!\n", name);
        goto error;
    }

    /**< TODO: Add proc handler functions in here */


    device->control = bzalloc(sizeof(struct mgw_ref));
    device->control->data = device;
    mgw_context_data_insert(&device->context,
        &mgw->data.devices_mutex, &mgw->data.devices_list);

    return device;

error:
    bfree(device->control);
    mgw_device_destroy(device);
    return NULL;
}

void mgw_device_addref(mgw_device_t *device)
{
    if (device && device->control)
        mgw_ref_addref(device->control);
}

void mgw_device_release(mgw_device_t *device)
{
    if (!device) return;

    struct mgw_ref *ctrl = device->control;
    if (mgw_ref_release(ctrl)) {
        mgw_device_destroy(device);
        bfree(ctrl);
    }
}

mgw_device_t *mgw_device_get_ref(mgw_device_t *device)
{
    return (device && device->control &&
            mgw_get_ref(device->control)) ? device : NULL;
}

mgw_device_t *mgw_get_weak_device(mgw_device_t *device)
{
    return (device && device->control) ? device->control->data : NULL;
}

bool mgw_device_references_device(
			struct mgw_ref *ref,mgw_device_t *device)
{
    return ref && device && device == ref->data;
}

const char *mgw_device_get_name(const mgw_device_t *device)
{
    return device ? device->context.obj_name : NULL;
}

int mgw_device_add_stream(mgw_device_t *device, const char *name, mgw_data_t *settings)
{
    if (!device || !name || !settings)
        return mgw_dev_err(MGW_ERR_EPARAM);

    if (!mgw_stream_create(device, name, settings)) {
        tlog(TLOG_ERROR, "Tried to create stream %s failed!\n", name);
        return mgw_dev_err(MGW_ERR_ECREATE);
    }

    return mgw_dev_err(MGW_ERR_SUCCESS);
}

int mgw_device_add_stream_obj(mgw_device_t *device, mgw_stream_t *stream)
{
    if (!device || !stream)
        return mgw_dev_err(MGW_ERR_EPARAM);

    if (mgw_get_weak_stream_by_name(device->stream_list,
            &stream->outputs_mutex,
            (const char *)stream->context.obj_name)) {
        tlog(TLOG_ERROR, "Already exist stream %s !\n", stream->context.obj_name);
        return mgw_dev_err(MGW_ERR_EXISTED);
    }
    mgw_context_data_insert(&stream->context,
			&device->stream_mutex, &device->stream_list);
    return mgw_dev_err(MGW_ERR_SUCCESS);
}

void mgw_device_release_stream(mgw_device_t *device, const char *name)
{
    if (!device || !name) return;

    mgw_stream_t *stream = mgw_get_weak_stream_by_name(
                    device->stream_list,&device->stream_mutex, name);
    if (stream)
        mgw_stream_release(stream);
}

bool mgw_device_send_packet(mgw_device_t *device,
						const char *stream_name,
						struct encoder_packet *packet)
{
    if (!device || !stream_name || !packet)
        return mgw_dev_err(MGW_ERR_EPARAM);

    mgw_stream_t *stream = mgw_get_stream_by_name(device->stream_list,
                            &device->stream_mutex, stream_name);
    if (!stream) {
        tlog(TLOG_ERROR, "Do not exist stream %s !\n", stream_name);
        return mgw_dev_err(MGW_ERR_INVALID_RES);
    }

    return mgw_stream_send_packet(stream, packet);
}