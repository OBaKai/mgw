#ifndef _PLUGINGS_SERVICE_MGW_SERVICE_H_
#define _PLUGINGS_SERVICE_MGW_SERVICE_H_

#include "util/c99defs.h"
#include "util/mgw-data.h"

#ifdef __cplusplus
extern "C" {
#endif

struct mgw_service;
typedef struct mgw_service mgw_service_t;

struct mgw_service_info
{
	const char		*id;

	const char		*(*get_name)(void *type);
	void			*(*create)(mgw_data_t *settings, mgw_service_t *service);
	void			(*destroy)(void *data);

	bool			(*start)(void *data);
	void			(*stop)(void *data);

	void			(*get_default)(mgw_data_t *settings);
	void			(*update)(void *data, mgw_data_t *settings);
	mgw_data_t		*(*get_setting)(void *data);
	//void            (*update_meta)(void *data, void *meta);
};

#ifdef __cplusplus
};
#endif

#endif  //_PLUGINGS_SERVICE_MGW_SERVICE_H_