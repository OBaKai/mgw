#ifndef _PLUGINS_SOURCE_MGW_SOURCES_H_
#define _PLUGINS_SOURCE_MGW_SOURCES_H_

#include "util/c99defs.h"
#include "util/mgw-data.h"
#include "util/codec-def.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MGW_SOURCE_VIDEO        (1 << 0)
#define MGW_SOURCE_AUDIO        (1 << 1)
#define MGW_SOURCE_AV           (MGW_SOURCE_VIDEO | MGW_SOURCE_AUDIO)
#define MGW_SOURCE_PICTURE      (1 << 2)
#define MGW_SOURCE_TEXT         (1 << 3)
#define MGW_SOURCE_LOCALFILE    (1 << 4)
#define MGW_SOUTCE_NETSTREAM    (1 << 5)

#define MGW_SOURCES_MAJOR_VER   1
#define MGW_SOURCES_MINOR_VER   0
#define MGW_SOURCES_PATCH_VER   0

struct mgw_source;
typedef struct mgw_source mgw_source_t;
typedef struct mgw_weak_source  mgw_weak_source_t;

struct mgw_source_info {
	const char              *id;
	uint32_t                output_flags;

	const char  *(*get_name)(void *type);
	void        *(*create)(mgw_data_t *setting, mgw_source_t *source);
	void        (*destroy)(void *data);

	bool        (*start)(void *data);
	void        (*stop)(void *data);

	void        (*get_defaults)(mgw_data_t *settings);
	void        (*update)(void *data, mgw_data_t *settings);
	mgw_data_t  *(*get_settings)(void *data);
    size_t      (*get_extra_data)(enum encoder_type type, uint8_t **data);
};

#ifdef __cplusplus
}
#endif
#endif  //_PLUGINS_SOURCE_MGW_SOURCES_H_