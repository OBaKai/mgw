#ifndef _MGW_CORE_MGW_INTERNAL_H_
#define _MGW_CORE_MGW_INTERNAL_H_

#include <pthread.h>

#include "mgw-defs.h"
#include "util/c99defs.h"
#include "util/darray.h"
#include "util/threading.h"
#include "util/codec-def.h"
#include "util/callback-handle.h"

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
	char *obj_name;
	char *info_id;
	void *info_impl;
	mgw_data_t *settings;
	//signal_handler_t *signals;
	proc_handler_t *procs;
	enum mgw_obj_type type;

	DARRAY(char *) rename_cache;
	pthread_mutex_t rename_cache_mutex;

	pthread_mutex_t *mutex;
	struct mgw_context_data *next;
	struct mgw_context_data **prev_next;

	bool	is_private;
};

extern bool mgw_context_data_init(
		void					*opaque,
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
	struct mgw_service		*services_list;
	struct mgw_stream		*priv_streams_list;
	struct mgw_device		*devices_list;

	pthread_mutex_t			services_mutex;
	pthread_mutex_t			priv_stream_mutex;
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

	struct mgw_core_data				data;
};

/* ------------------------------------------------------------------------- */
/* ref-counting  */
struct mgw_ref {
	volatile long refs;
	void *data;
};

static inline void mgw_ref_addref(struct mgw_ref *ref)
{
	os_atomic_inc_long(&ref->refs);
}

static inline bool mgw_ref_release(struct mgw_ref *ref)
{
	return os_atomic_dec_long(&ref->refs) == -1;
}

static inline bool mgw_get_ref(struct mgw_ref *ref)
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
/* Source */
struct mgw_source {
	struct mgw_context_data		context;
	struct mgw_source_info		info;
    struct mgw_ref				*control;
	struct mgw_stream			*parent_stream;

	uint32_t                    flags;
	bool                        is_private;
	volatile bool               enabled;
	volatile bool				actived;

	int                         reconnect_retry_sec;
	int                         reconnect_retry_max;
	int                         reconnect_retries;
	int                         reconnect_retry_cur_sec;
	pthread_t                   reconnect_thread;
	os_event_t                  *reconnect_stop_event;
	volatile bool               reconnecting;
	volatile bool               reconnect_thread_active;

	os_event_t					*stopping_event;
	pthread_t                   stop_thread;
	int							stop_code;
    int                         last_error_status;
	void						*buffer;

	struct bmem					audio_header, video_header;
	enum encoder_id				audio_payload, video_payload;

	bool	(*active)(mgw_source_t *source);
	void	(*output_packet)(mgw_source_t *source, struct encoder_packet *pkt);
	void	(*update_settings)(mgw_source_t *source, mgw_data_t *settings);
};

extern struct mgw_source_info *get_source_info(const char *id);
extern bool mgw_source_init_context(struct mgw_source *source,
		mgw_data_t *settings, const char *source_name, bool is_private);
extern void mgw_source_destroy(struct mgw_source *source);

/* ----------------------------------------- */
/* Output */
struct mgw_output {
	struct mgw_context_data		context;
	struct mgw_output_info		info;
    struct mgw_ref				*control;
	struct mgw_stream			*parent_stream;

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
	int							stop_code;
	os_event_t					*stopping_event;
	void						*buffer;
	volatile long				failed_count;
	pthread_t					stop_thread;
	int							last_error_status;

	proc_handler_t		*(*get_source_proc_handler)(mgw_output_t *output);
	int					(*get_encoder_packet)(mgw_output_t *output, encoder_packet_t *packet);
};

extern const struct mgw_output_info *find_output_info(const char *id);
void mgw_output_destroy(mgw_output_t *output);

/* ----------------------------------------- */
/* Format */
struct mgw_format {

};

/* ----------------------------------------- */
/* Service */
enum mgw_service_type {
	MGW_SERVICE_RTMPIN,
	MGW_SERVICE_RTMPOUT,
	MGW_SERVICE_SRTIN,
	MGW_SERVICE_SRTOUT,
};

struct mgw_service {
	struct mgw_context_data		context;
	struct mgw_service_info		info;
	struct mgw_ref				*control;

	enum mgw_service_type		type;
};

/* ----------------------------------------- */
/* Stream */
struct mgw_stream {
    struct mgw_context_data		context;
    struct mgw_ref				*control;
    struct mgw_device			*parent_device;

    struct mgw_source			*source;
    struct mgw_output			*outputs_list;
    struct mgw_output			*outputs_whitelist;
    struct mgw_output           *outputs_blacklist;

    pthread_mutex_t				outputs_mutex;
    pthread_mutex_t				outputs_whitelist_mutex;
    pthread_mutex_t				outputs_blacklist_mutex;

    volatile bool				actived;
};
typedef struct mgw_stream mgw_stream_t;

/* ----------------------------------------- */
/* Device */
struct mgw_device {
	struct mgw_context_data		context;
	struct mgw_ref				*control;

	struct mgw_stream			*stream_list;
	pthread_mutex_t				stream_mutex;

	cb_handle_t					proc_cb_handle;
};
typedef struct mgw_device mgw_device_t;

#endif  //_MGW_CORE_MGW_INTERNAL_H_
