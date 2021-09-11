#ifndef _PLUGINS_OUTPUTS_MGW_OUTPUTS_H_
#define _PLUGINS_OUTPUTS_MGW_OUTPUTS_H_

#include "util/darray.h"
#include "util/c99defs.h"
#include "util/mgw-data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MGW_OUTPUT_VIDEO       (1<<0)
#define MGW_OUTPUT_AUDIO       (1<<1)
#define MGW_OUTPUT_AV          (MGW_OUTPUT_VIDEO | MGW_OUTPUT_AUDIO)
#define MGW_OUTPUT_ENCODED     (1<<2)
#define MGW_OUTPUT_SERVICE     (1<<3)

#define MGW_OUTPUTS_MAJOR_VER   1
#define MGW_OUTPUTS_MINOR_VER   0
#define MGW_OUTPUTS_PATCH_VER   0

struct mgw_output;
typedef struct mgw_output mgw_output_t;

struct mgw_output_info {
    const char      *id;
    uint32_t        flags;

    const char      *(*get_name)(void *type);
    void            *(*create)(mgw_data_t *setting, mgw_output_t *output);
    void            (*destroy)(void *data);
    bool            (*start)(void *data);
    void            (*stop)(void *data);
    uint64_t        (*get_total_bytes)(void *data);

    /** Options */
    mgw_data_t      *(*get_settings)(void *data);
    mgw_data_t      *(*get_default)(void);
    void            (*update)(void *data, mgw_data_t *settings);
};

#ifdef __cplusplus
};
#endif
#endif  //_PLUGINS_OUTPUTS_MGW_OUTPUTS_H_