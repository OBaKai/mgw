#include "mgw.h"

static void mgw_service_destroy(struct mgw_service *service)
{

}

mgw_service_t *mgw_service_create(const char *id, const char *name, mgw_data_t *settings)
{
    return NULL;
}

const char *mgw_service_get_name(const mgw_service_t *service)
{
    return NULL;
}

bool mgw_service_start(mgw_service_t *service)
{
    return true;
}

/** Stops the service. */
void mgw_service_stop(mgw_service_t *service)
{

}

void mgw_service_addref(mgw_service_t *service)
{
    if (!service) return;
    mgw_ref_addref(service->control);
}

void mgw_service_release(mgw_service_t *service)
{
    if (!service) return;

    struct mgw_ref *ref = service->control;
    if (mgw_ref_release(ref)) {
        mgw_service_destroy(service);
        bfree(ref);
    }
}

mgw_service_t *mgw_service_get_ref(mgw_service_t *service)
{
    if (!service) return NULL;
    return mgw_get_ref(service->control) ? service : NULL;
}

mgw_service_t *mgw_get_weak_service(mgw_service_t *service)
{
    if (!service) return NULL;
	return (mgw_service_t*)service->control->data;
}

bool mgw_service_references_service(
			struct mgw_ref *ref,mgw_service_t *service)
{
    return ref && service && ref->data == service;
}

mgw_source_t *mgw_service_create_source(mgw_service_t *service, mgw_data_t *setting)
{
    return NULL;
}

void mgw_service_release_source(mgw_service_t *service, mgw_source_t *source)
{

}

mgw_output_t *mgw_service_create_output(mgw_service_t *service, mgw_data_t *setting)
{
    return NULL;
}

void mgw_service_release_output(mgw_service_t *service, mgw_output_t *output)
{

}