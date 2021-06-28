#include "mgw-internal.h"
#include "mgw-module.h"

#include "util/darray.h"

/* ---------------------------------- */
/* sources */
extern struct mgw_module sources_module;

/* ---------------------------------- */
/* outputs */
extern struct mgw_module outputs_module;

static inline bool mgw_module_check_and_load_necessary_val(\
					struct mgw_module *info,\
					struct darray *modules,
					struct darray *info_types, size_t type_size)
{
	#define check_output_required_val(info, val) \
		module_check_required_val(struct mgw_module, \
			info, val, mgw_module_check_and_load_necessary_val);

	check_output_required_val(info, load);
	check_output_required_val(info, unload);

	#undef check_output_required_val

	if (!info->loaded)
		info->loaded = info->load(info_types, type_size);

	DARRAY(struct mgw_module) *mods = (DARRAY(struct mgw_module) *)modules;
	module_register_def(struct mgw_module, (*mods), info);
	return true;
error:
	return false;
}

void mgw_load_all_modules(struct mgw_core *core)
{
	if (!core) {
		blog(MGW_LOG_ERROR, "mgw_core is invalid!\n");
		return;
	}
	/* Sources */
	mgw_module_check_and_load_necessary_val(\
			&sources_module, (struct darray*)&core->modules,\
			(struct darray*)&core->source_types, sizeof(struct mgw_source_info));
	/* Outputs */
	mgw_module_check_and_load_necessary_val(\
			&outputs_module,(struct darray*)&core->modules,\
			(struct darray*)&core->output_types, sizeof(struct mgw_output_info));
}

mgw_module_t *mgw_find_module(struct mgw_core *core, const char *name)
{
	mgw_module_t *mod = NULL;
	if (!core || !name)
		return NULL;

	for (int i = 0; i < core->modules.num; i++) {
		mod = core->modules.array + i;
		if (0 == strncmp(name, mod->id, strlen(name)))
			break;
	}
	return mod;
}

const char *mgw_get_module_name(mgw_module_t *module)
{
	return module ? module->id : NULL;
}