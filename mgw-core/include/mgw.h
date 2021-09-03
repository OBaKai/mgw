#ifndef _MGW_CORE_MGW_H_
#define _MGW_CORE_MGW_H_

#include "util/mgw-data.h"
#include "mgw-defs.h"
#include "mgw-internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * mgw startup by module configuration path and properties 
 */
bool mgw_startup(mgw_data_t *store);
void mgw_shutdown(void);
bool mgw_initialized(void);

uint32_t mgw_get_version(void);
const char *mgw_get_version_string(void);
const char *mgw_get_type(void);
const char *mgw_get_sn(void);

/**
 * load all module automatically
 */
void mgw_load_all_modules(struct mgw_core *core);
//int mgw_open_module(mgw_module_t **module, const char *path, const char *data_path);
//bool mgw_init_module(mgw_module_t *module);
//void mgw_add_module_path(const char* bin, const char *data);
mgw_module_t *mgw_find_module(struct mgw_core *core, const char *name);
const char *mgw_get_module_name(mgw_module_t *module);
//const char *mgw_get_module_binary_path(mgw_module_t *module);
//const char mgw_get_module_data_path(mgw_module_t *module);

mgw_output_t *mgw_get_output_by_name(mgw_output_t *output_list,
					pthread_mutex_t *mutex, const char *name);
mgw_output_t *mgw_get_weak_output_by_name(mgw_output_t *output_list,
						pthread_mutex_t *mutex, const char *name);
mgw_service_t *mgw_get_service_by_name(const char *name);
mgw_stream_t *mgw_get_priv_stream_by_name(const char *name);
mgw_stream_t *mgw_get_stream_by_name(mgw_stream_t *stream_list,
						pthread_mutex_t *mutex, const char *name);
mgw_stream_t *mgw_get_weak_stream_by_name(mgw_stream_t *stream_list,
						pthread_mutex_t *mutex, const char *name);
mgw_device_t *mgw_get_device_by_name(const char *name);

bool mgw_get_source_info(struct mgw_source_info *msi);
bool mgw_get_output_info(struct mgw_output_info *moi);
bool mgw_get_format_info(struct mgw_format_info *mfi);

proc_handler_t *mgw_source_get_procs(void *source);
proc_handler_t *mgw_output_get_procs(void *output);
proc_handler_t *mgw_stream_get_procs(void *stream);
proc_handler_t *mgw_device_get_procs(void *device);

/** Sources */
mgw_data_t *mgw_save_source(mgw_source_t *source);
mgw_source_t *mgw_load_source(mgw_data_t *data);

typedef void (*mgw_load_source_cb)(void *private_data, mgw_source_t *source);
void mgw_laod_sources(mgw_data_array_t *array, mgw_load_source_cb cb, void *private_data);
mgw_data_array_t *mgw_save_sources(void);

/** Outputs */
mgw_data_t *mgw_save_output(mgw_output_t *source);
mgw_output_t *mgw_load_output(mgw_data_t *data);

typedef void (*mgw_load_output_cb)(void *private_data, mgw_output_t *source);
void mgw_load_outputs(mgw_data_array_t *array, mgw_load_output_cb cb, void *private_data);
mgw_data_array_t *mgw_save_outputs(void);


/***********************************
 * Source operations
 **********************************/
mgw_source_t *mgw_source_create(struct mgw_stream *stream,
				const char *name, mgw_data_t *settings);
bool mgw_source_is_private(mgw_source_t *source);

void mgw_source_addref(mgw_source_t *source);
void mgw_source_release(mgw_source_t *source);
mgw_source_t *mgw_source_get_ref(mgw_source_t *source);
mgw_source_t *mgw_get_weak_source(mgw_source_t *source);
bool mgw_source_references_source(
			struct mgw_ref *ref,mgw_source_t *source);

/** For netstream and local file */
bool mgw_source_start(struct mgw_source *source);
void mgw_source_stop(struct mgw_source *source);

void mgw_source_update_settings(mgw_source_t *source, mgw_data_t *settings);
void mgw_source_write_packet(mgw_source_t *source, struct encoder_packet *packet);

void mgw_source_set_video_extra_data(
        mgw_source_t *source, uint8_t *data, size_t size);
void mgw_source_set_audio_extra_data(mgw_source_t *source,
		uint8_t channels, uint8_t samplesize, uint32_t samplerate);


/***********************************
 * Output operations
 **********************************/

mgw_output_t *mgw_output_create(struct mgw_stream *stream,
				const char *output_name, mgw_data_t *settings);
/**
 * Adds/releases a reference to an output.  When the last reference is
 * released, the output is destroyed.
 */
void mgw_output_addref(mgw_output_t *output);
void mgw_output_release(mgw_output_t *output);
mgw_output_t *mgw_output_get_ref(mgw_output_t *output);
mgw_output_t *mgw_get_weak_output(mgw_output_t *output);
bool mgw_output_references_output(
			struct mgw_ref *ref,mgw_output_t *output);

const char *mgw_output_get_name(const mgw_output_t *output);
mgw_data_t *mgw_output_get_state(mgw_output_t *output);

bool mgw_output_start(mgw_output_t *output);
void mgw_output_stop(mgw_output_t *output);
void mgw_output_release_all(mgw_output_t *output_list);


/***********************************
 * Service operations
 **********************************/
mgw_service_t *mgw_service_create(const char *id, const char *name, mgw_data_t *settings);

void mgw_service_addref(mgw_service_t *service);
void mgw_service_release(mgw_service_t *service);
mgw_service_t *mgw_service_get_ref(mgw_service_t *service);
mgw_service_t *mgw_get_weak_service(mgw_service_t *service);
bool mgw_service_references_service(
			struct mgw_ref *ref,mgw_service_t *service);

const char *mgw_service_get_name(const mgw_service_t *service);

/** Starts the service. */
bool mgw_service_start(mgw_service_t *service);

/** Stops the service. */
void mgw_service_stop(mgw_service_t *service);

mgw_source_t *mgw_service_create_source(mgw_service_t *service, mgw_data_t *setting);
void mgw_service_release_source(mgw_service_t *service, mgw_source_t *source);

mgw_output_t *mgw_service_create_output(mgw_service_t *service, mgw_data_t *setting);
void mgw_service_release_output(mgw_service_t *service, mgw_output_t *output);

/***********************************
 * Stream operations
 **********************************/
mgw_stream_t *mgw_stream_create(mgw_device_t *device,
		const char *stream_name, mgw_data_t *settings);
void mgw_stream_stop(mgw_stream_t *stream);
void mgw_stream_release_all(mgw_stream_t *stream_list);

void mgw_stream_addref(mgw_stream_t *stream);
void mgw_stream_release(mgw_stream_t *stream);
mgw_stream_t *mgw_stream_get_ref(mgw_stream_t *stream);
mgw_stream_t *mgw_get_weak_stream(mgw_stream_t *stream);
bool mgw_stream_references_stream(
			struct mgw_ref *ref,mgw_stream_t *stream);

const char *mgw_stream_get_name(const mgw_stream_t *stream);

int mgw_stream_add_source(mgw_stream_t *stream, mgw_data_t *source_settings);
void mgw_stream_release_source(mgw_stream_t *stream);

int mgw_stream_add_output(mgw_stream_t *stream, mgw_data_t *output_settings);
void mgw_stream_release_output(mgw_stream_t *stream, mgw_output_t *output);
void mgw_stream_release_output_by_name(mgw_stream_t *stream, const char *id);

mgw_data_t *mgw_stream_get_output_info(mgw_stream_t *stream, const char *output_name);
mgw_data_t *mgw_stream_get_output_setting(mgw_stream_t *stream, const char *id);

bool mgw_stream_send_packet(mgw_stream_t *stream, struct encoder_packet *packet);

/***********************************
 * Device operations
 **********************************/
mgw_device_t *mgw_device_create(const char *id, const char *name, mgw_data_t *settings);

void mgw_device_addref(mgw_device_t *device);
void mgw_device_release(mgw_device_t *device);
mgw_device_t *mgw_device_get_ref(mgw_device_t *device);
mgw_device_t *mgw_get_weak_device(mgw_device_t *device);
bool mgw_device_references_device(
			struct mgw_ref *ref,mgw_device_t *device);

const char *mgw_device_get_name(const mgw_device_t *device);

int mgw_device_add_stream(mgw_device_t *device, const char *id, mgw_data_t *settings);
int mgw_device_add_stream_obj(mgw_device_t *device, mgw_stream_t *stream);
void mgw_device_release_stream(mgw_device_t *device, const char *id);
bool mgw_device_send_packet(mgw_device_t *device,
						const char *stream_name,
						struct encoder_packet *packet);


int mgw_reset_streams(mgw_device_t *device, mgw_data_t *stream_settings);
int mgw_reset_all(void);

#ifdef __cplusplus
};
#endif
#endif  //_MGW_CORE_MGW_H_