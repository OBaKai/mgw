#ifndef _MGW_CORE_MGW_INTERNAL_H_
#define _MGW_CORE_MGW_INTERNAL_H_

#include <pthread.h>

#include "mgw-defs.h"
#include "util/c99defs.h"
#include "util/darray.h"
#include "util/threading.h"
#include "util/codec-def.h"

#include "outputs/mgw-outputs.h"
#include "sources/mgw-sources.h"
#include "formats/mgw-formats.h"
#include "services/mgw-services.h"

struct mgw_module {
	char *id;
	//const char *file;
	//char *bin_path;
	//char *data_path;
	//void *module;
	bool loaded;

	bool        (*load)(struct darray *types, size_t type_size);
	void        (*unload)(struct darray *types, size_t type_size);
	//void        (*post_load)(void);
	uint32_t    (*version)(void);
	//void        (*set_pointer)(struct mgw_module *module);
	const char *(*name)(void);
	const char *(*description)(void);
	//const char *(*author)(void);

	//struct mgw_module *next;
};
typedef struct mgw_module mgw_module_t;


/* ------------------------------------------------------------------------- */
/* mgw shared context data */
struct mgw_context_data {
	char *name;
	char *id;
    /** Store info implement */
	void *data;
	mgw_data_t *settings;
	//signal_handler_t *signals;
	//proc_handler_t *procs;
	enum mgw_obj_type type;

	DARRAY(char *) rename_cache;
	pthread_mutex_t rename_cache_mutex;

	pthread_mutex_t *mutex;
	struct mgw_context_data *next;
	struct mgw_context_data **prev_next;

	bool	is_private;
};

extern bool mgw_context_data_init(
		struct mgw_context_data *context,
		enum mgw_obj_type       type,
		mgw_data_t              *settings,
		const char              *name,
		bool                    is_private);
extern void mgw_context_data_free(struct mgw_context_data *context);

extern void mgw_context_data_insert(struct mgw_context_data *context,
		pthread_mutex_t *mutex, void *first);
extern void mgw_context_data_remove(struct mgw_context_data *context);

extern void mgw_context_data_setname(struct mgw_context_data *context,
		const char *name);


struct mgw_core_data {
	struct mgw_source		*first_source;
	struct mgw_output		*first_output;
	struct mgw_format		*first_format;
	struct mgw_service		*first_service;
	struct mgw_device		*first_device;


	pthread_mutex_t			sources_mutex;
	pthread_mutex_t			outputs_mutex;
	pthread_mutex_t			formats_mutex;
	pthread_mutex_t			services_mutex;
	pthread_mutex_t			devices_mutex;

	long long               unnamed_index;
	mgw_data_t				*private_data;
	volatile bool			valid;
};

struct mgw_core {
	DARRAY(struct mgw_module)			modules;

    DARRAY(struct mgw_source_info)      source_types;
	DARRAY(struct mgw_format_info)		format_types;
	DARRAY(struct mgw_output_info)		output_types;
	DARRAY(struct mgw_service_info)		service_types;

	struct mgw_core_data		data;
};

/* ------------------------------------------------------------------------- */
/* ref-counting  */

struct mgw_weak_ref {
	volatile long refs;
	volatile long weak_refs;
};

static inline void mgw_ref_addref(struct mgw_weak_ref *ref)
{
	os_atomic_inc_long(&ref->refs);
}

static inline bool mgw_ref_release(struct mgw_weak_ref *ref)
{
	return os_atomic_dec_long(&ref->refs) == -1;
}

static inline void mgw_weak_ref_addref(struct mgw_weak_ref *ref)
{
	os_atomic_inc_long(&ref->weak_refs);
}

static inline bool mgw_weak_ref_release(struct mgw_weak_ref *ref)
{
	return os_atomic_dec_long(&ref->weak_refs) == -1;
}

static inline bool mgw_weak_ref_get_ref(struct mgw_weak_ref *ref)
{
	long owners = ref->refs;
	while (owners > -1) {
		if (os_atomic_compare_swap_long(&ref->refs, owners, owners + 1))
			return true;

		owners = ref->refs;
	}

	return false;
}

/* ----------------------------------------- */
/* Format */
struct mgw_format {


};

struct mgw_weak_source {
	struct mgw_source *source;
    struct mgw_weak_ref ref;
};

struct mgw_source {
	/* source data contex */
	struct mgw_context_data		context;
	/* source informat operations */
	struct mgw_source_info		info;
    struct mgw_weak_source      *control;

	uint32_t                    flags;
	bool                        enabled;
	/** Encoder Source without source_info, this field set to true*/
	bool                        private_source;
	/** Flag to identify source whether started */
	bool						actived;

	os_event_t					*stopping_event;
	os_event_t					*reconnect_stop_event;

	int							stop_code;
	void						*buffer;

	struct bmem					audio_header, video_header;
	enum encoder_id				audio_payload, video_payload;

	bool	(*active)(mgw_source_t *source);
	void	(*output_packet)(mgw_source_t *source, struct encoder_packet *pkt);
	void	(*update_settings)(mgw_source_t *source, mgw_data_t *settings);
};

extern struct mgw_source_info *get_source_info(const char *id);
extern bool mgw_source_init_context(struct mgw_source *source,
		mgw_data_t *settings, const char *name, bool is_private);
extern void mgw_source_destroy(struct mgw_source *source);

/* ----------------------------------------- */
/* Output */

struct mgw_weak_output {
	struct mgw_output *output;
    struct mgw_weak_ref ref;
};

typedef struct mgw_weak_output mgw_weak_output_t;

struct mgw_output {
	struct mgw_context_data		context;
	struct mgw_output_info		info;
    struct mgw_weak_output      *control;

	int                         reconnect_retry_sec;
	int                         reconnect_retry_max;
	int                         reconnect_retries;
	int                         reconnect_retry_cur_sec;
	pthread_t                   reconnect_thread;
	os_event_t                  *reconnect_stop_event;
	volatile bool               reconnecting;
	volatile bool               reconnect_thread_active;

	volatile bool				actived;
	bool						valid;
	bool						private_output;
	int							stop_code;
	os_event_t					*stopping_event;
	void						*buffer;
	volatile long				failed_count;
	pthread_t					stop_thread;
	void						*cb_data;

	int			last_error_status;

	bool		(*active)(mgw_output_t *output);
	void		(*signal_stop)(mgw_output_t *output, int code);
	bool		(*source_is_ready)(mgw_output_t *output);
	mgw_data_t	*(*get_encoder_settings)(mgw_output_t *output);
	size_t		(*get_video_header)(mgw_output_t *output, uint8_t **header);
	size_t		(*get_audio_header)(mgw_output_t *output, uint8_t **header);
	int			(*get_next_encoder_packet)(mgw_output_t *output, struct encoder_packet *packet);
};

extern const struct mgw_output_info *find_output(const char *id);
void mgw_output_destroy(mgw_output_t *output);

/* ----------------------------------------- */
/* Service */

enum mgw_service_type {
	MGW_SERVICE_RTMPIN,
	MGW_SERVICE_RTMPOUT,
	MGW_SERVICE_SRTIN,
	MGW_SERVICE_SRTOUT,
};

struct mgw_weak_service {
	struct mgw_weak_ref ref;
	struct mgw_service *service;
};

typedef struct mgw_weak_service mgw_weak_service_t;

struct mgw_service {
	struct mgw_context_data		context;
	struct mgw_service_info		info;
	enum mgw_service_type		type;

	DARRAY(struct mgw_source)	sources;
	DARRAY(struct mgw_output)	outputs;
};


/* ----------------------------------------- */
/* Device */

struct mgw_weak_device {
	struct mgw_weak_ref ref;
	struct mgw_device *device;
};

typedef struct mgw_weak_device mgw_weak_device_t;

struct mgw_device {
	struct mgw_context_data		context;

	DARRAY(struct mgw_source)	sources;
	DARRAY(struct mgw_output)	outputs;
};
typedef struct mgw_device mgw_device_t;

#endif  //_MGW_CORE_MGW_INTERNAL_H_
