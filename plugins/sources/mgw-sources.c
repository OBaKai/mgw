#include "mgw-sources.h"
#include "mgw-internal.h"
#include "mgw-module.h"

#include "util/darray.h"

#define SOURCES_DESCRIPTION     "sources: [ffmpeg-source]"

extern struct mgw_source_info ffmpeg_source_info;

static inline bool check_and_register_source_info(\
            struct mgw_source_info *info, struct darray *sources)
{
	#define check_source_required_val(info, val) \
		module_check_required_val(struct mgw_output_info, \
			info, val, check_and_register_source_info)

	check_source_required_val(info, get_name);
	check_source_required_val(info, create);
	check_source_required_val(info, destroy);
	check_source_required_val(info, start);
	check_source_required_val(info, stop);
	#undef check_source_required_val

	DARRAY(struct mgw_source_info) *srcs = sources;
	module_register_def(struct mgw_source_info, (*srcs), info);

    return true;

error:
	return false;
}

bool sources_load(struct darray *sources, size_t type_size)
{
	if (!sources || (type_size != \
		sizeof(struct mgw_source_info)))
		return false;

	/** 
	 * Register all source here
	*/
	check_and_register_source_info(&ffmpeg_source_info, sources);

	return true;
}

void sources_unload(struct darray *sources, size_t type_size)
{
	if (!sources || (type_size != \
			sizeof(struct mgw_source_info)))
		return;

	DARRAY(struct mgw_source_info) *dest = sources;
	for (size_t i = 0; i < sources->num; i++) {
		struct mgw_output_info *info = sources->array + i;
		/** Unload ffmpeg_source_info */
		if (0 == memcmp(info, &ffmpeg_source_info, \
			sizeof(ffmpeg_source_info))) {
			da_erase_item((*dest), info);
		}
	}
}

uint32_t sources_get_version(void)
{
	return MGW_MAKE_VERSION_INT(MGW_SOURCES_MAJOR_VER,\
				MGW_SOURCES_MINOR_VER, MGW_SOURCES_PATCH_VER);
}

const char *sources_description(void)
{
	return SOURCES_DESCRIPTION;
}


struct mgw_module sources_module = {
	.id				= "mgw-sources",
	.load			= sources_load,
	.unload			= sources_unload,
	.version		= sources_get_version,
	.description	= sources_description
};