#include "mgw.h"
#include "util/tlog.h"

extern struct mgw_core *mgw;

void mgw_device_proc_cb_handle(mgw_device_t *device,
            int type, int status, void *data, size_t size)
{
    if (device && device->proc_cb_handle)
        device->proc_cb_handle(type, status, data, size);
}

static void mgw_device_destroy(mgw_device_t *device)
{
    if (device->stream_list)
        mgw_stream_release_all(device->stream_list);

    mgw_context_data_free(&device->context);
    pthread_mutex_destroy(&device->stream_mutex);

    bfree(device);
}

mgw_device_t *mgw_device_create(const char *type, const char *sn,
            mgw_data_t *settings, cb_handle_t status_cb)
{
    if (!sn || !settings) return NULL;

    mgw_device_t *device = bzalloc(sizeof(struct mgw_device));
    device->proc_cb_handle = status_cb;

    if (0 != pthread_mutex_init(&device->stream_mutex, NULL))
        goto error;

    if (!mgw_context_data_init(device, &device->context,
                MGW_OBJ_TYPE_DEVEICE, settings, sn, false)) {
        tlog(TLOG_ERROR, "Initialize %s device %s context failed!\n", type?type:"", sn);
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

bool mgw_device_has_stream(mgw_device_t *device)
{
    return device && device->stream_list;
}

int mgw_device_add_stream(mgw_device_t *device, const char *name, mgw_data_t *settings)
{
    int16_t ret = 0;
    if (!device || !name || !settings)
        return mgw_dev_err(MGW_ERR_EPARAM);

    mgw_stream_t *stream = NULL;
    if (!(stream = mgw_stream_create(device, name, settings))) {
        tlog(TLOG_ERROR, "Tried to create stream %s failed!\n", name);
        return mgw_dev_err(MGW_ERR_ECREATE);
    }

    /**< Add a source to stream if have source settings and stream haven't source */
    if (mgw_data_has_user_value(settings, "source") &&
        !mgw_stream_has_source(stream)) {
        mgw_data_t *source_settings = mgw_data_get_obj(settings, "source");

        ret = mgw_stream_add_source(stream, source_settings);
        mgw_data_release(source_settings);
        if (ret) return ret;
    }

    if (mgw_data_has_user_value(settings, "outputs")) {
        mgw_data_array_t *outputs = mgw_data_get_array(settings, "outputs");
        size_t outputs_cnt = mgw_data_array_count(outputs);
        for (size_t i = 0; i < outputs_cnt; i++) {
            mgw_data_t *output_settings = mgw_data_array_item(outputs, i);
            if ((ret = mgw_stream_add_output(stream, output_settings)))
                tlog(TLOG_ERROR, "Stream[%s] tried to add output failed!", name);

            mgw_data_release(output_settings);
        }
        mgw_data_array_release(outputs);
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

int mgw_device_addstream_with_outputs(mgw_device_t *device, mgw_data_t *stream_settings)
{
    const char *name = mgw_data_get_string(stream_settings, "name");
    return mgw_device_add_stream(device, name, stream_settings);
}

int mgw_device_addstream_with_source(mgw_device_t *device, mgw_data_t *stream_settings)
{
    const char *name = mgw_data_get_string(stream_settings, "name");
    return mgw_device_add_stream(device, name, stream_settings);
}

/**< Add output stream to default stream if stream_name is NULL */
int mgw_device_add_output_to_stream(mgw_device_t *device,
			const char *stream_name, mgw_data_t *output_info)
{
    if (!device || !stream_name || !output_info)
        return mgw_dev_err(MGW_ERR_EPARAM);

    mgw_stream_t *stream = NULL;
    if (!(stream = mgw_get_weak_stream_by_name(device->stream_list,
                            &device->stream_mutex, stream_name))) {
        tlog(TLOG_ERROR, "Counldn't find stream[%s]", stream_name);
        return mgw_dev_err(MGW_ERR_NOT_EXIST);
    }

    return mgw_stream_add_output(stream, output_info);
}

/**< Release output stream from default stream if stream_name is NULL */
void mgw_device_release_output_from_stream(mgw_device_t *device,
			const char *stream_name, mgw_data_t *output_info)
{
    if (!device || !stream_name || !output_info)
        return;

    mgw_stream_t *stream = NULL;
    if ((stream = mgw_get_weak_stream_by_name(device->stream_list,
                            &device->stream_mutex, stream_name))) {
        const char *output_name = mgw_data_get_string(output_info, "output_name");
        mgw_stream_release_output_by_name(stream, output_name);
    }
}

bool mgw_device_send_packet(mgw_device_t *device,
						const char *stream_name,
						struct encoder_packet *packet)
{
    if (!device || !stream_name || !packet)
        return mgw_dev_err(MGW_ERR_EPARAM);

    mgw_stream_t *stream = mgw_get_weak_stream_by_name(device->stream_list,
                            &device->stream_mutex, stream_name);
    if (!stream) {
        tlog(TLOG_ERROR, "Do not exist stream %s !", stream_name);
        return mgw_dev_err(MGW_ERR_INVALID_RES);
    }

    return mgw_stream_send_packet(stream, packet);
}