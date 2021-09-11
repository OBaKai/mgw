#include "mgw.h"
#include "mgw-module.h"
#include "mgw-defs.h"
#include "util/base.h"
#include "util/tlog.h"
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

    blog(MGW_MGW_LOG_DEBUG, "----git sha1: %s\n", sha1);

    return sha1;
}*/

static char *make_version_str(uint8_t major, uint8_t minor, \
                uint16_t patch, bool with_time, bool with_sha1)
{
	char *version = bzalloc(128);

	snprintf(version, 128, "%d.%d.%d", major, minor, patch);

	if (with_time) {
		char *time_str = get_localtime_str();
		if (time_str) {
			strcat(version, "_");
			strncat(version, time_str, 32);
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

const char *mgw_get_type(void)
{
	return mgw ? mgw_data_get_string(mgw->data.private_data, "type") : NULL;
}

const char *mgw_get_sn(void)
{
	return mgw ? mgw_data_get_string(mgw->data.private_data, "sn") : NULL;
}

const char *mgw_get_version_string(void)
{
	static char mgw_ver[128] = {};
	memset(mgw_ver, 0, sizeof(mgw_ver));
	char *ver = make_version_str(LIBMGW_API_MAJOR_VER, LIBMGW_API_MINOR_VER,\
					LIBMGW_API_PATCH_VER, false, false);
	if (ver) {
		strncpy(mgw_ver, ver, sizeof(mgw_ver) - 1);
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
	if (0 != pthread_mutex_init(&data->priv_streams_list, &attr))
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

	log_system_info();
	if (!mgw_init_data())
		return false;

	if (mgw->data.valid && data)
		mgw_data_apply(mgw->data.private_data, data);

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
		tlog(TLOG_WARN, "mgw aready exist! Tried to call mgw_startup more than once");
		return false;
	}

	if (!mgw_init(store)) {
		mgw_shutdown();
		return false;
	}

	if (!mgw_start_services())
		tlog(TLOG_INFO, "All services invalid!");

	return true;
}

static void destroy_all_services(void)
{
	mgw_service_t *service = mgw->data.services_list;
	while(service) {
		mgw_service_t *next = (mgw_service_t*)service->context.next;
		mgw_service_stop(service);
		mgw_service_release(service);
		service = next;
	}
}

void mgw_output_release_all(mgw_output_t *output_list)
{
	mgw_output_t *output = output_list;
	while (output) {
		mgw_output_t *next = (mgw_output_t *)output->context.next;
		mgw_output_stop(output);
		mgw_output_release(output);
		output = next;
	}
	output_list = NULL;
}

void mgw_stream_release_all(mgw_stream_t *stream_list)
{
	mgw_stream_t *stream = stream_list;
	while (stream) {
		mgw_stream_t *next = (mgw_stream_t *)stream->context.next;
		mgw_stream_stop(stream);
		mgw_stream_release(stream);
		stream = next;
	}
	stream_list = NULL;
}

void mgw_device_release_all(mgw_device_t *device_list)
{
	mgw_device_t *device = device_list;
	while (device) {
		mgw_device_t *next = (mgw_stream_t *)device->context.next;
		mgw_device_release(device);
		device = next;
	}
	device_list = NULL;
}

static void free_mgw_data(void)
{
	struct mgw_core_data *data = &mgw->data;
	pthread_mutex_destroy(&data->priv_stream_mutex);
	pthread_mutex_destroy(&data->services_mutex);
	pthread_mutex_destroy(&data->devices_mutex);

	if (data->private_data) {
		mgw_data_release(data->private_data);
		data->private_data = NULL;
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
	da_free(mgw->modules);

	destroy_all_services();
	mgw_stream_release_all(mgw->data.priv_streams_list);
	mgw_device_release_all(mgw->data.devices_list);
	free_mgw_data();

	bfree(mgw);
}

int mgw_reset_streams(mgw_device_t *device, mgw_data_t *stream_settings)
{
	if (!stream_settings) return -1;

	int16_t ret = 0;
	const char *stream_name = mgw_data_get_string(stream_settings, "name");
	mgw_stream_t *stream = mgw_stream_create(device, stream_name, stream_settings);

	mgw_data_t *source_settings = mgw_data_get_obj(stream_settings, "source");
	if (source_settings) {
		if ((ret = mgw_stream_add_source(stream, source_settings)))
			tlog(TLOG_ERROR, "Tried to add source to stream:%s failed!\n", stream_name);

		mgw_data_release(source_settings);
	}

	/**< create all outputs! */
	mgw_data_array_t *outputs_settings = mgw_data_get_array(stream_settings, "outputs");
	if (outputs_settings) {
		size_t outputs_cnt = mgw_data_array_count(outputs_settings);
		for (int i = 0; i < outputs_cnt; i++) {
			mgw_data_t *output_settings = mgw_data_array_item(outputs_settings, i);
			if (output_settings) {
				if ((ret = mgw_stream_add_output(stream, output_settings)))
					tlog(TLOG_ERROR, "Tried to add output to stream:%s failed!\n", stream_name);

				mgw_data_release(output_settings);
			}
		}
		mgw_data_array_release(outputs_settings);
	}

	return 0;
}

int mgw_reset_all(void)
{
	if (!mgw) return -1;

	int ret = 0;
	struct mgw_core *core = mgw;
	mgw_data_array_t *devices = mgw_data_get_array(core->data.private_data, "devices");
	if (devices) {
		size_t devices_cnt = mgw_data_array_count(devices);
		for (int i = 0; i < devices_cnt; i++) {
			mgw_data_t *dev = mgw_data_array_item(devices, i);
			/** create device */
			const char *type = mgw_data_get_string(dev, "type");
			const char *sn = mgw_data_get_string(dev, "sn");
			mgw_device_t *dev_impl = mgw_device_create(type, sn, dev);
			mgw_data_array_t *streams = mgw_data_get_array(dev, "streams");
			if (streams) {
				size_t streams_cnt = mgw_data_array_count(streams);
				for (int j = 0; j < streams_cnt; j++) {
					mgw_data_t *stream = mgw_data_array_item(streams, i);

					// mgw_device_add_stream(dev_impl, stream);

					if (0 != (ret = mgw_reset_streams(dev_impl, stream)))
						tlog(TLOG_INFO, "Tried to reset stream:%s "\
								"failed, ret:%d\n", mgw_data_get_string(stream, "name"), ret);
					mgw_data_release(stream);;
				}
			}
			mgw_data_array_release(streams);
			mgw_data_release(dev);
		}
		mgw_data_array_release(devices);
	}

	/**< Reset all private streams */
	mgw_data_array_t *private_streams = mgw_data_get_array(core->data.private_data, "private_streams");
	if (private_streams) {
		int streams_cnt = mgw_data_array_count(private_streams);
		for (int i = 0; i < streams_cnt; i++) {
			mgw_data_t *stream = mgw_data_array_item(private_streams, i);
			if (0 != (ret = mgw_reset_streams(NULL, stream)))
				tlog(TLOG_INFO, "Tried to reset stream:%s "\
						"failed, ret:%d\n", mgw_data_get_string(stream, "name"), ret);
		}
	}
	return ret;
}

/* get every type of operation and change reference */
/* source, output, format, stream, service, device */
static inline void *get_context_by_name(void *vfirst, const char *name,
		pthread_mutex_t *mutex, void *(*addref)(void*))
{
	struct mgw_context_data **first = vfirst;
	struct mgw_context_data *context;

	pthread_mutex_lock(mutex);
	context = *first;
	while (context) {
		if (!context->is_private &&
			strcmp(context->obj_name, name) == 0) {
			if (addref)
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
		void					*opaque,
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

	context->procs = proc_handler_create(opaque);
	if (!context->procs)
		return false;

    context->info_id	= bstrdup(mgw_data_get_string(settings, "id"));
	context->obj_name	= dup_name(name, is_private);
	context->settings	= mgw_data_newref(settings);
	return true;
}

bool mgw_context_data_init(
		void					*opaque,
		struct mgw_context_data *context,
		enum mgw_obj_type       type,
		mgw_data_t              *settings,
		const char              *name,
		bool                    is_private)
{
	if (mgw_context_data_init_wrap(
				opaque, context, type,
				settings, name, is_private)) {
		return true;
	} else {
		mgw_context_data_free(context);
		return false;
	}
}

void mgw_context_data_free(struct mgw_context_data *context)
{
	// signal_handler_destroy(context->signals);
	proc_handler_destroy(context->procs);
	mgw_data_release(context->settings);
	mgw_context_data_remove(context);
	pthread_mutex_destroy(&context->rename_cache_mutex);
	bfree(context->obj_name);
	bfree(context->info_id);

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

	if (context->obj_name)
		da_push_back(context->rename_cache, &context->obj_name);
	context->obj_name = dup_name(name, context->is_private);

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
static inline void *mgw_stream_addref_safe_(void *ref)
{
	return mgw_stream_get_ref(ref);
}

static inline void *mgw_id_(void *data)
{
	return data;
}

mgw_output_t *mgw_get_output_by_name(mgw_output_t *output_list,
						pthread_mutex_t *mutex, const char *name)
{
	if (!output_list || !name) return NULL;
	return get_context_by_name(&output_list,
			name, mutex, mgw_output_addref_safe_);
}

mgw_output_t *mgw_get_weak_output_by_name(mgw_output_t *output_list,
						pthread_mutex_t *mutex, const char *name)
{
	if (!output_list || !name) return NULL;
	return get_context_by_name(&output_list, name, mutex, NULL);
}

mgw_service_t *mgw_get_service_by_name(const char *name)
{
	if (!mgw) return NULL;
	return get_context_by_name(&mgw->data.services_list, name,
			&mgw->data.services_mutex, mgw_service_addref_safe_);
}

mgw_stream_t *mgw_get_priv_stream_by_name(const char *name)
{
	if (!mgw) return NULL;
	return get_context_by_name(&mgw->data.priv_streams_list, name,
			&mgw->data.devices_mutex, mgw_stream_addref_safe_);
}

mgw_stream_t *mgw_get_stream_by_name(mgw_stream_t *stream_list,
						pthread_mutex_t *mutex, const char *name)
{
	if (!stream_list || !name) return NULL;
	return get_context_by_name(&stream_list, name, mutex, mgw_stream_addref_safe_);
}

mgw_stream_t *mgw_get_weak_stream_by_name(mgw_stream_t *stream_list,
						pthread_mutex_t *mutex, const char *name)
{
	if (!stream_list || !name) return NULL;
	return get_context_by_name(&stream_list, name, mutex, NULL);
}

mgw_device_t *mgw_get_device_by_name(const char *name)
{
	if (!mgw) return NULL;
	return get_context_by_name(&mgw->data.devices_list, name,
			&mgw->data.devices_mutex, mgw_device_addref_safe_);
}

mgw_device_t *mgw_get_weak_device_by_name(const char *name)
{
	if (!mgw) return NULL;
	return get_context_by_name(&mgw->data.devices_list, name,
			&mgw->data.devices_mutex, NULL);
}

bool mgw_get_source_info(struct mgw_source_info *msi)
{
    return false;
}

bool mgw_get_output_info(struct mgw_output_info *moi)
{
    return false;
}

bool mgw_get_format_info(struct mgw_format_info *mfi)
{
    return false;
}

proc_handler_t *mgw_source_get_procs(void *source)
{
	return source ? ((mgw_source_t*)source)->context.procs : NULL;
}

proc_handler_t *mgw_output_get_procs(void *output)
{
	return output ? ((mgw_output_t*)output)->context.procs : NULL;
}

proc_handler_t *mgw_stream_get_procs(void *stream)
{
	return stream ? ((mgw_stream_t*)stream)->context.procs : NULL;
}
proc_handler_t *mgw_device_get_procs(void *device)
{
	return device ? ((mgw_device_t*)device)->context.procs : NULL;
}

/** Sources */
mgw_data_t *mgw_save_source(mgw_source_t *source)
{
    return NULL;
}

mgw_source_t *mgw_load_source(mgw_data_t *data)
{
    return NULL;
}

void mgw_laod_sources(mgw_data_array_t *array,
			mgw_load_source_cb cb, void *private_data)
{

}

mgw_data_array_t *mgw_save_sources(void)
{
    return NULL;
}

/** Outputs */
mgw_data_t *mgw_save_output(mgw_output_t *source)
{
    return NULL;
}

mgw_output_t *mgw_load_output(mgw_data_t *data)
{
    return NULL;
}

void mgw_load_outputs(mgw_data_array_t *array,
			mgw_load_output_cb cb, void *private_data)
{

}

mgw_data_array_t *mgw_save_outputs(void)
{
	return NULL;
}