#include "mgw.h"


mgw_service_t *mgw_service_create(const char *id, const char *name, mgw_data_t *settings)
{
    return NULL;
}

void mgw_service_addref(mgw_service_t *service)
{

}

void mgw_service_release(mgw_service_t *service)
{

}

mgw_service_t *mgw_service_get_ref(mgw_service_t *service)
{
    return NULL;
}
mgw_weak_service_t *mgw_service_get_weak_service(mgw_service_t *service)
{
    return NULL;
}

mgw_service_t *mgw_weak_service_get_service(mgw_weak_service_t *weak)
{
    return NULL;
}

bool mgw_weak_service_references_service(mgw_weak_service_t *weak,
		mgw_service_t *service)
{
    return false;
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