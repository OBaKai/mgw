#ifndef _CORE_UTIL_CALLBACK_HANDLE_H_
#define _CORE_UTIL_CALLBACK_HANDLE_H_

#include "darray.h"

struct call_params;
typedef int (*cb_proc)(void*, struct call_params *params);

struct call_data {
	const char	*name;
	cb_proc		cb_proc_func;
};

typedef struct proc_handler {
	void	*opaque;
	DARRAY(struct call_data) proc_functions;
}proc_handler_t;

typedef struct call_params {
    void    *in;
    size_t  in_size;
    void    *out;
    size_t  out_size;
}call_params_t;

proc_handler_t *proc_handler_create(void* opaque);
void proc_handler_destroy(proc_handler_t *handler);

bool proc_handler_add(proc_handler_t *handler, const char *name, cb_proc func);
void proc_handler_remove(proc_handler_t *handler, const char *name);

int proc_handler_do(proc_handler_t *handler, const char *name, struct call_params *params);

#endif  //_CORE_UTIL_CALLBACK_HANDLE_H_