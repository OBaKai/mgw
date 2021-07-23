#include <string.h>

#include "mgw-module.h"
#include "mgw-formats.h"
#include "mgw-internal.h"

#define FORMATS_DESCRIPTION		"formats: [mpegts-format, flv-format]"

extern struct mgw_format_info mpegts_format_info;
//extern struct mgw_format_info flv_format_info;

static inline bool check_and_register_format_info( \
		struct mgw_format_info *info, struct darray *formats)
{
	#define check_format_required_val(info, val) \
		module_check_required_val(struct mgw_format_info, \
			info, val, check_and_register_format_info)

	check_format_required_val(info, get_name);
	check_format_required_val(info, create);
	check_format_required_val(info, destroy);
	check_format_required_val(info, start);
	check_format_required_val(info, stop);
	check_format_required_val(info, send_packet);
	#undef check_format_required_val

	DARRAY(struct mgw_format_info) *outs = formats;
	module_register_def(struct mgw_format_info, (*outs), info);

    return true;

error:
	return false;
}

bool formats_load(struct darray *formats, size_t type_size)
{
	if (!formats || (type_size != \
			sizeof(struct mgw_format_info)))
		return false;
	/* register all format here */
	check_and_register_format_info(&mpegts_format_info, formats);
    //check_and_register_format_info(&flv_format_info, formats);

	return true;
}

void formats_unload(struct darray *formats, size_t type_size)
{
	if (!formats || (type_size != \
			sizeof(struct mgw_format_info)))
		return;

	size_t info_size = sizeof(struct mgw_format);

	DARRAY(struct mgw_format_info) *dest = formats;
	for (size_t i = 0; i < formats->num; i++) {
		struct mgw_format_info *info = formats->array + i;
		if (0 == memcmp(info, &mpegts_format_info, info_size)/* ||
			0 == memcmp(info, &flv_format_info, info_size)*/) {
			da_erase_item((*dest), info);
		}
	}
}

uint32_t formats_get_version(void)
{
	return MGW_MAKE_VERSION_INT(MGW_FORMATS_MAJOR_VER,
			MGW_FORMATS_MINOR_VER, MGW_FORMATS_PATCH_VER);
}

const char *formats_description(void)
{
	return FORMATS_DESCRIPTION;
}

struct mgw_module formats_module = {
	.id					= "formats-module",
	.load				= formats_load,
	.unload				= formats_unload,
	.version			= formats_get_version,
	.description		= formats_description
};