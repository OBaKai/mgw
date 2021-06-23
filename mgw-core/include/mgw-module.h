#ifndef _MGW_CORE_MGW_MODULE_H_
#define _MGW_CORE_MGW_MODULE_H_

#include "util/base.h"
#include "util/darray.h"

#ifdef __cplusplus
#define MODULE_EXPORT extern "C" EXPORT
#define MODULE_EXTERN extern "C"
#else
#define MODULE_EXPORT EXPORT
#define MODULE_EXTERN extern
#endif

#define module_check_required_val(type, info, val, func)  do { \
		if ((offsetof(type, val) + sizeof(info->val) > sizeof(type)) || \
			!info->val) { \
			blog(LOG_ERROR, "Required value" #val "for %s not found." #func \
							" failed.", info->id); \
			goto error; \
		} \
	} while(false)

#define module_register_def(type, dest, info) do { \
		type data = {0}; \
		memcpy(&data, info, sizeof(data)); \
		da_push_back(dest, &data); \
	} while(false)


#endif  //_MGW_CORE_MGW_MODULE_H_