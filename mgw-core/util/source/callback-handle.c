#include "callback-handle.h"
#include "bmem.h"

proc_handler_t *proc_handler_create(void* opaque)
{
	struct proc_handler *handler = bzalloc(sizeof(struct proc_handler));
	da_init(handler->proc_functions);
	handler->opaque = opaque;
	return handler;
}

static inline proc_info_free(struct call_data *info)
{
	bfree(info->name);
	info->cb_proc_func = NULL;
}

void proc_handler_destroy(proc_handler_t *handler)
{
	if (handler) {
		for (int i = 0; i < handler->proc_functions.num; i++)
			proc_info_free(handler->proc_functions.array + i);
		da_free(handler->proc_functions);
		bfree(handler);
	}
}

static inline struct call_data *get_proc_data(proc_handler_t *handler,
					const char *name, cb_proc func)
{
	for (int i = 0; i < handler->proc_functions.num; i++)
		if (!strncmp((handler->proc_functions.array+i)->name,
			name, strlen(name)) ||
			(handler->proc_functions.array+i)->cb_proc_func == func)
			return handler->proc_functions.array+i;

	return NULL;
}

static inline int get_proc_data_idx(proc_handler_t *handler,
					const char *name, cb_proc func)
{
	for (int i = 0; i < handler->proc_functions.num; i++)
		if (!strncmp((handler->proc_functions.array+i)->name,
			name, strlen(name)) ||
			(handler->proc_functions.array+i)->cb_proc_func == func)
			return i;

	return -1;
}

bool proc_handler_add(proc_handler_t *handler, const char *name, cb_proc func)
{
	if (!handler || !name || !func)
		return false;

	if (!get_proc_data(handler, name, func)) {
		struct call_data data = {bstrdup(name), func};
		da_push_back(handler->proc_functions, &data);
	}
	return true;
}

void proc_handler_remove(proc_handler_t *handler, const char *name)
{
	int idx = -1;
	if (handler && name)
		if ((idx = get_proc_data_idx(handler, name, NULL)) >= 0)
			da_erase(handler->proc_functions, idx);
}

int proc_handler_do(proc_handler_t *handler, const char *name, struct call_params *params)
{
	struct call_data *data = NULL;
	if (handler && name)
		if ((data = get_proc_data(handler, name, NULL)))
			return data->cb_proc_func(handler->opaque, params);

	return -1;
}