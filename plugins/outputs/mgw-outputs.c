#include <string.h>

#include "mgw-module.h"
#include "mgw-outputs.h"
#include "mgw-internal.h"

#define OUTPUTS_DESCRIPTION		"outputs: [rtmp-output]"

extern struct mgw_output_info rtmp_output_info;

static inline bool check_and_register_output_info( \
		struct mgw_output_info *info, struct darray *outputs)
{
	#define check_output_required_val(info, val) \
		module_check_required_val(struct mgw_output_info, \
			info, val, check_and_register_output_info)

	check_output_required_val(info, get_name);
	check_output_required_val(info, create);
	check_output_required_val(info, destroy);
	check_output_required_val(info, start);
	check_output_required_val(info, stop);
	check_output_required_val(info, get_total_bytes);
	#undef check_output_required_val

	DARRAY(struct mgw_output_info) *outs = outputs;
	module_register_def(struct mgw_output_info, (*outs), info);

    return true;

error:
	return false;
}

bool outputs_load(struct darray *outputs, size_t type_size)
{
	if (!outputs || (type_size != \
			sizeof(struct mgw_output_info)))
		return false;
	/* register all output here */
	check_and_register_output_info(&rtmp_output_info, outputs);

	return true;
}

void outputs_unload(struct darray *outputs, size_t type_size)
{
	if (!outputs || (type_size != \
			sizeof(struct mgw_output_info)))
		return;

	DARRAY(struct mgw_output_info) *dest = outputs;
	for (size_t i = 0; i < outputs->num; i++) {
		struct mgw_output_info *info = outputs->array + i;
		if (0 == memcmp(info, &rtmp_output_info, \
			sizeof(rtmp_output_info))) {
			da_erase_item((*dest), info);
		}
	}
}

uint32_t outputs_get_version(void)
{
	return MGW_MAKE_VERSION_INT(MGW_OUTPUTS_MAJOR_VER,\
			MGW_OUTPUTS_MINOR_VER, MGW_OUTPUTS_PATCH_VER);
}

const char *outputs_description(void)
{
	return OUTPUTS_DESCRIPTION;
}


struct mgw_module outputs_module = {
	.id					= "outputs-module",
	.load				= outputs_load,
	.unload				= outputs_unload,
	.version			= outputs_get_version,
	.description		= outputs_description
};