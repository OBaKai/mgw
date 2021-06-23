#include "mgw.h"
#include "mgw-module.h"
#include "mgw-defs.h"
#include "util/base.h"
#include "util/dstr.h"
#include "util/mgw-data.h"
#include "util/platform.h"
#include "util/threading.h"

#include <stdio.h>
#include <assert.h>

struct mgw_core *mgw = NULL;


bool mgw_initialized(void)
{
	return mgw != NULL;
}

/*static const char *git_head = "../../.git/HEAD";
static const char *get_git_sha1(void)
{
    FILE *git_file = NULL;
    char buffer[1024] = {};
    static char sha1[8] = {};

    if (access(git_head, F_OK))
        return NULL;

    git_file = fopen(git_head, "r");
    if (!git_file)
        return NULL;

    char *pos = fgets(buffer, sizeof(buffer), git_file);
    if (!pos)
        return NULL;

    memcpy(sha1, buffer, 6);

    blog(LOG_DEBUG, "----git sha1: %s\n", sha1);

    return sha1;
}*/

static char *make_version_str(uint8_t major, uint8_t minor, \
                uint16_t patch, bool with_time, bool with_sha1)
{
	char *version = bmalloc(64);

	snprintf(version, 64, "%d.%d.%d", major, minor, patch);

	if (with_time) {
		char *time_str = get_localtime_str();
		if (time_str) {
			strcat(version, "_");
			strncat(version, time_str, 32);
			bfree(time_str);
		}
	}

	if (with_sha1) {
		strcat(version, "_");
		strncat(version, LIBMGW_API_SHA1, 8);
	}

	return version;
}

uint32_t mgw_get_version(void)
{
	return MGW_MAKE_VERSION_INT(LIBMGW_API_MAJOR_VER, LIBMGW_API_MINOR_VER,\
						LIBMGW_API_PATCH_VER);
}

const char *mgw_get_version_string(void)
{
	static char mgw_ver[64] = {};
	char *ver = make_version_str(LIBMGW_API_MAJOR_VER, LIBMGW_API_MINOR_VER,\
					LIBMGW_API_PATCH_VER, true, true);
	if (mgw_ver) {
		strncpy(mgw_ver, ver, sizeof(mgw_ver));
		bfree(ver);
	}
	return mgw_ver;
}

static bool mgw_init_data(void)
{
	struct mgw_core_data *data = &mgw->data;
	pthread_mutexattr_t attr;

	if (0 != pthread_mutexattr_init(&attr))
		return false;
	if (0 != pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE))
		goto failed;
	if (0 != pthread_mutex_init(&data->sources_mutex, &attr))
		goto failed;
	if (0 != pthread_mutex_init(&data->formats_mutex, &attr))
		goto failed;
	if (0 != pthread_mutex_init(&data->outputs_mutex, &attr))
		goto failed;
	if (0 != pthread_mutex_init(&data->services_mutex, &attr))
		goto failed;
	if (0 != pthread_mutex_init(&data->devices_mutex, &attr))
		goto failed;

	data->private_data = mgw_data_create();
	data->valid = !!data->private_data;

failed:
	pthread_mutexattr_destroy(&attr);
	return data->valid;
}

static bool mgw_init(mgw_data_t *data)
{
	mgw = bzalloc(sizeof(struct mgw_core));

	/** log system enviroment */
	log_system_info();
	/** initialize mgw data */
	if (!mgw_init_data())
		return false;

	if (mgw->data.valid && data)
		mgw_data_apply(mgw->data.private_data, data);

	/** callback handlers initialize ??? */

	/** load and register all modules */
	mgw_load_all_modules(mgw);

	return true;
}

static bool mgw_start_services(void)
{
    return false;
}

bool mgw_startup(mgw_data_t *store)
{
	if (mgw) {
		blog(LOG_WARNING, "mgw aready exist! Tried to call mgw_startup more than once");
		return false;
	}

	if (!mgw_init(store)) {
		mgw_shutdown();
		return false;
	}

	if (!mgw_start_services())
		blog(LOG_INFO, "All services invalid!");

	return true;
}

static void destroy_all_services(void)
{
	mgw_service_t *service = mgw->data.first_service;
	while(service) {
		mgw_service_t *next = (mgw_service_t*)service->context.next;
		mgw_service_stop(service);
		mgw_service_release(service);
		service = next;
	}
}

static void destroy_all_outputs(void)
{
	mgw_output_t *output = mgw->data.first_output;
	while(output) {
		mgw_output_t *next = (mgw_output_t*)output->context.next;
		mgw_output_stop(output);
		mgw_output_release(output);
		output = next;
	}
}

static void destroy_all_sources(void)
{
	mgw_source_t *source = mgw->data.first_source;
	while(source) {
		mgw_source_t *next = (mgw_source_t*)source->context.next;
		mgw_source_stop(source);
		mgw_source_release(source);
		source = next;
	}
	mgw->data.first_source = NULL;
}

static void free_mgw_modules(void)
{

}

static void free_mgw_data(void)
{
	struct mgw_core_data *data = &mgw->data;
	pthread_mutex_destroy(&data->sources_mutex);
	pthread_mutex_destroy(&data->outputs_mutex);
	pthread_mutex_destroy(&data->services_mutex);
	pthread_mutex_destroy(&data->formats_mutex);
	pthread_mutex_destroy(&data->devices_mutex);

	if (data->private_data) {
		mgw_data_release(data->private_data);
		data->valid = false;
	}
}

void mgw_shutdown(void)
{
	struct mgw_core *core = NULL;
	if (!mgw)
		return;

	da_free(mgw->source_types);
	da_free(mgw->format_types);
	da_free(mgw->output_types);
	da_free(mgw->service_types);

	destroy_all_services();
	destroy_all_outputs();
	destroy_all_sources();
	da_free(mgw->modules);
	free_mgw_data();

	core = mgw;
	mgw = NULL;

	bfree(core);
}

/* get every type of operation and change reference */
/* source, output, format, service, device */

static inline void *get_context_by_name(void *vfirst, const char *name,
		pthread_mutex_t *mutex, void *(*addref)(void*))
{
	struct mgw_context_data **first = vfirst;
	struct mgw_context_data *context;

	pthread_mutex_lock(mutex);

	context = *first;
	while (context) {
		if (!context->is_private && strcmp(context->name, name) == 0) {
			context = addref(context);
			break;
		}
		context = context->next;
	}

	pthread_mutex_unlock(mutex);
	return context;
}

/** ----------------------------------------------- */
/** context data operations */

/* ensures that names are never blank */
static inline char *dup_name(const char *name, bool private)
{
	if (private && !name)
		return NULL;

	if (!name || !*name) {
		struct dstr unnamed = {0};
		dstr_printf(&unnamed, "__unnamed%04lld",
				mgw->data.unnamed_index++);

		return unnamed.array;
	} else {
		return bstrdup(name);
	}
}

static inline bool mgw_context_data_init_wrap(
		struct mgw_context_data *context,
		enum mgw_obj_type       type,
		mgw_data_t              *settings,
		const char              *name,
		bool                    is_private)
{
	assert(context);
	memset(context, 0, sizeof(*context));
	context->is_private = is_private;
	context->type = type;

	pthread_mutex_init_value(&context->rename_cache_mutex);
	if (pthread_mutex_init(&context->rename_cache_mutex, NULL) < 0)
		return false;

	/*context->signals = signal_handler_create();
	if (!context->signals)
		return false;
    */

	/*context->procs = proc_handler_create();
	if (!context->procs)
		return false;
    */

	context->name        = dup_name(name, is_private);
	context->settings    = mgw_data_newref(settings);
	return true;
}

bool mgw_context_data_init(
		struct mgw_context_data *context,
		enum mgw_obj_type       type,
		mgw_data_t              *settings,
		const char              *name,
		bool                    is_private)
{
	if (mgw_context_data_init_wrap(context, type, settings,
                    name, is_private)) {
		return true;
	} else {
		mgw_context_data_free(context);
		return false;
	}
}

void mgw_context_data_free(struct mgw_context_data *context)
{
	//mgw_hotkeys_context_release(context);
	//signal_handler_destroy(context->signals);
	//proc_handler_destroy(context->procs);
	mgw_data_release(context->settings);
	mgw_context_data_remove(context);
	pthread_mutex_destroy(&context->rename_cache_mutex);
	bfree(context->name);

	for (size_t i = 0; i < context->rename_cache.num; i++)
		bfree(context->rename_cache.array[i]);
	da_free(context->rename_cache);

	memset(context, 0, sizeof(*context));
}

void mgw_context_data_insert(struct mgw_context_data *context,
		pthread_mutex_t *mutex, void *pfirst)
{
	struct mgw_context_data **first = pfirst;

	assert(context);
	assert(mutex);
	assert(first);

	context->mutex = mutex;

	pthread_mutex_lock(mutex);
	context->prev_next  = first;
	context->next       = *first;
	*first              = context;
	if (context->next)
		context->next->prev_next = &context->next;
	pthread_mutex_unlock(mutex);
}

void mgw_context_data_remove(struct mgw_context_data *context)
{
	if (context && context->mutex) {
		pthread_mutex_lock(context->mutex);
		if (context->prev_next)
			*context->prev_next = context->next;
		if (context->next)
			context->next->prev_next = context->prev_next;
		pthread_mutex_unlock(context->mutex);

		context->mutex = NULL;
	}
}

void mgw_context_data_setname(struct mgw_context_data *context,
		const char *name)
{
	pthread_mutex_lock(&context->rename_cache_mutex);

	if (context->name)
		da_push_back(context->rename_cache, &context->name);
	context->name = dup_name(name, context->is_private);

	pthread_mutex_unlock(&context->rename_cache_mutex);
}

/** ----------------------------------------------------- */

static inline void *mgw_source_addref_safe_(void *ref)
{
	return mgw_source_get_ref(ref);
}

static inline void *mgw_output_addref_safe_(void *ref)
{
	return mgw_output_get_ref(ref);
}

static inline void *mgw_service_addref_safe_(void *ref)
{
	return mgw_service_get_ref(ref);
}

static inline void *mgw_device_addref_safe_(void *ref)
{
	return mgw_device_get_ref(ref);
}

static inline void *mgw_id_(void *data)
{
	return data;
}

mgw_source_t *mgw_get_source_by_name(const char *name)
{
	if (!mgw) return NULL;
	return get_context_by_name(&mgw->data.first_source, name,
			&mgw->data.sources_mutex, mgw_source_addref_safe_);
}

mgw_output_t *mgw_get_output_by_name(const char *name)
{
	if (!mgw) return NULL;
	return get_context_by_name(&mgw->data.first_output, name,
			&mgw->data.outputs_mutex, mgw_output_addref_safe_);
}

mgw_service_t *mgw_get_service_by_name(const char *name)
{
	if (!mgw) return NULL;
	return get_context_by_name(&mgw->data.first_service, name,
			&mgw->data.services_mutex, mgw_service_addref_safe_);
}

mgw_device_t *mgw_get_device_by_name(const char *name)
{
	if (!mgw) return NULL;
	return get_context_by_name(&mgw->data.first_device, name,
			&mgw->data.devices_mutex, mgw_device_addref_safe_);
}


bool mgw_get_source_info(struct mgw_source_info *msi)
{

}

bool mgw_get_output_info(struct mgw_output_info *moi)
{

}

bool mgw_get_format_info(struct mgw_format_info *mfi)
{

}

/** Sources */
mgw_data_t *mgw_save_source(mgw_source_t *source)
{

}

mgw_source_t *mgw_load_source(mgw_data_t *data)
{

}

void mgw_laod_sources(mgw_data_array_t *array, mgw_load_source_cb cb, void *private_data)
{

}

mgw_data_array_t *mgw_save_sources(void)
{

}

/** Outputs */
mgw_data_t *mgw_save_output(mgw_output_t *source)
{

}

mgw_output_t *mgw_load_output(mgw_data_t *data)
{

}

void mgw_laod_outputs(mgw_data_array_t *array, mgw_load_output_cb cb, void *private_data)
{

}

mgw_data_array_t *mgw_save_outputs(void)
{
	
}