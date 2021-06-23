#include "mgw.h"



mgw_device_t *mgw_device_create(const char *id, const char *name, mgw_data_t *settings)
{
    return NULL;
}

void mgw_device_addref(mgw_device_t *device)
{

}

void mgw_device_release(mgw_device_t *device)
{

}

mgw_device_t *mgw_device_get_ref(mgw_device_t *device)
{
    return NULL;
}

mgw_weak_device_t *mgw_device_get_weak_device(mgw_device_t *device)
{
    return NULL;
}

mgw_device_t *mgw_weak_device_get_device(mgw_weak_device_t *weak)
{
    return NULL;
}

bool mgw_weak_device_referencesdevice(mgw_weak_device_t *weak,
		mgw_device_t *service)
{
    return false;
}

const char *mgw_device_get_name(const mgw_device_t *device)
{
    return NULL;
}

bool mgw_device_add_source(mgw_device_t *device, mgw_source_t *source)
{
    return false;
}

bool mgw_device_add_output(mgw_device_t *device, mgw_output_t *output)
{
    return false;
}