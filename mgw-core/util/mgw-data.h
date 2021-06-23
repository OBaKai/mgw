#pragma once

#include "c99defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OBS data settings storage
 *
 *   This is used for retrieving or setting the data settings for things such
 * as sources, encoders, etc.  This is designed for JSON serialization.
 */

struct mgw_data;
struct mgw_data_item;
struct mgw_data_array;
typedef struct mgw_data       mgw_data_t;
typedef struct mgw_data_item  mgw_data_item_t;
typedef struct mgw_data_array mgw_data_array_t;

enum mgw_data_type {
	MGW_DATA_NULL,
	MGW_DATA_STRING,
	MGW_DATA_NUMBER,
	MGW_DATA_BOOLEAN,
	MGW_DATA_OBJECT,
	MGW_DATA_ARRAY
};

enum mgw_data_number_type {
	MGW_DATA_NUM_INVALID,
	MGW_DATA_NUM_INT,
	MGW_DATA_NUM_DOUBLE
};

/* ------------------------------------------------------------------------- */
/* Main usage functions */

EXPORT mgw_data_t *mgw_data_create();
EXPORT mgw_data_t *mgw_data_create_from_json(const char *json_string);
EXPORT mgw_data_t *mgw_data_create_from_json_file(const char *json_file);
EXPORT mgw_data_t *mgw_data_create_from_json_file_safe(const char *json_file,
		const char *backup_ext);
EXPORT void mgw_data_addref(mgw_data_t *data);
EXPORT void mgw_data_release(mgw_data_t *data);

EXPORT const char *mgw_data_get_json(mgw_data_t *data);
EXPORT bool mgw_data_save_json(mgw_data_t *data, const char *file);
EXPORT bool mgw_data_save_json_safe(mgw_data_t *data, const char *file,
		const char *temp_ext, const char *backup_ext);

EXPORT void mgw_data_apply(mgw_data_t *target, mgw_data_t *apply_data);

EXPORT void mgw_data_erase(mgw_data_t *data, const char *name);
EXPORT void mgw_data_clear(mgw_data_t *data);

/* Set functions */
EXPORT void mgw_data_set_string(mgw_data_t *data, const char *name,
		const char *val);
EXPORT void mgw_data_set_int(mgw_data_t *data, const char *name,
		long long val);
EXPORT void mgw_data_set_double(mgw_data_t *data, const char *name, double val);
EXPORT void mgw_data_set_bool(mgw_data_t *data, const char *name, bool val);
EXPORT void mgw_data_set_obj(mgw_data_t *data, const char *name, mgw_data_t *obj);
EXPORT void mgw_data_set_array(mgw_data_t *data, const char *name,
		mgw_data_array_t *array);

/*
 * Default value functions.
 */
EXPORT void mgw_data_set_default_string(mgw_data_t *data, const char *name,
		const char *val);
EXPORT void mgw_data_set_default_int(mgw_data_t *data, const char *name,
		long long val);
EXPORT void mgw_data_set_default_double(mgw_data_t *data, const char *name,
		double val);
EXPORT void mgw_data_set_default_bool(mgw_data_t *data, const char *name,
		bool val);
EXPORT void mgw_data_set_default_obj(mgw_data_t *data, const char *name,
		mgw_data_t *obj);

/*
 * Application overrides
 * Use these to communicate the actual values of settings in case the user
 * settings aren't appropriate
 */
EXPORT void mgw_data_set_autoselect_string(mgw_data_t *data, const char *name,
		const char *val);
EXPORT void mgw_data_set_autoselect_int(mgw_data_t *data, const char *name,
		long long val);
EXPORT void mgw_data_set_autoselect_double(mgw_data_t *data, const char *name,
		double val);
EXPORT void mgw_data_set_autoselect_bool(mgw_data_t *data, const char *name,
		bool val);
EXPORT void mgw_data_set_autoselect_obj(mgw_data_t *data, const char *name,
		mgw_data_t *obj);

/*
 * Get functions
 */
EXPORT const char *mgw_data_get_string(mgw_data_t *data, const char *name);
EXPORT long long mgw_data_get_int(mgw_data_t *data, const char *name);
EXPORT double mgw_data_get_double(mgw_data_t *data, const char *name);
EXPORT bool mgw_data_get_bool(mgw_data_t *data, const char *name);
EXPORT mgw_data_t *mgw_data_get_obj(mgw_data_t *data, const char *name);
EXPORT mgw_data_array_t *mgw_data_get_array(mgw_data_t *data, const char *name);

EXPORT const char *mgw_data_get_default_string(mgw_data_t *data,
		const char *name);
EXPORT long long mgw_data_get_default_int(mgw_data_t *data, const char *name);
EXPORT double mgw_data_get_default_double(mgw_data_t *data, const char *name);
EXPORT bool mgw_data_get_default_bool(mgw_data_t *data, const char *name);
EXPORT mgw_data_t *mgw_data_get_default_obj(mgw_data_t *data, const char *name);
EXPORT mgw_data_array_t *mgw_data_get_default_array(mgw_data_t *data,
		const char *name);

EXPORT const char *mgw_data_get_autoselect_string(mgw_data_t *data,
		const char *name);
EXPORT long long mgw_data_get_autoselect_int(mgw_data_t *data, const char *name);
EXPORT double mgw_data_get_autoselect_double(mgw_data_t *data, const char *name);
EXPORT bool mgw_data_get_autoselect_bool(mgw_data_t *data, const char *name);
EXPORT mgw_data_t *mgw_data_get_autoselect_obj(mgw_data_t *data,
		const char *name);
EXPORT mgw_data_array_t *mgw_data_get_autoselect_array(mgw_data_t *data,
		const char *name);

/* Array functions */
EXPORT mgw_data_array_t *mgw_data_array_create();
EXPORT void mgw_data_array_addref(mgw_data_array_t *array);
EXPORT void mgw_data_array_release(mgw_data_array_t *array);

EXPORT size_t mgw_data_array_count(mgw_data_array_t *array);
EXPORT mgw_data_t *mgw_data_array_item(mgw_data_array_t *array, size_t idx);
EXPORT size_t mgw_data_array_push_back(mgw_data_array_t *array, mgw_data_t *obj);
EXPORT void mgw_data_array_insert(mgw_data_array_t *array, size_t idx,
		mgw_data_t *obj);
EXPORT void mgw_data_array_push_back_array(mgw_data_array_t *array,
		mgw_data_array_t *array2);
EXPORT void mgw_data_array_erase(mgw_data_array_t *array, size_t idx);

/* ------------------------------------------------------------------------- */
/* Item status inspection */

EXPORT bool mgw_data_has_user_value(mgw_data_t *data, const char *name);
EXPORT bool mgw_data_has_default_value(mgw_data_t *data, const char *name);
EXPORT bool mgw_data_has_autoselect_value(mgw_data_t *data, const char *name);

EXPORT bool mgw_data_item_has_user_value(mgw_data_item_t *data);
EXPORT bool mgw_data_item_has_default_value(mgw_data_item_t *data);
EXPORT bool mgw_data_item_has_autoselect_value(mgw_data_item_t *data);

/* ------------------------------------------------------------------------- */
/* Clearing data values */

EXPORT void mgw_data_unset_user_value(mgw_data_t *data, const char *name);
EXPORT void mgw_data_unset_default_value(mgw_data_t *data, const char *name);
EXPORT void mgw_data_unset_autoselect_value(mgw_data_t *data, const char *name);

EXPORT void mgw_data_item_unset_user_value(mgw_data_item_t *data);
EXPORT void mgw_data_item_unset_default_value(mgw_data_item_t *data);
EXPORT void mgw_data_item_unset_autoselect_value(mgw_data_item_t *data);

/* ------------------------------------------------------------------------- */
/* Item iteration */

EXPORT mgw_data_item_t *mgw_data_first(mgw_data_t *data);
EXPORT mgw_data_item_t *mgw_data_item_byname(mgw_data_t *data, const char *name);
EXPORT bool mgw_data_item_next(mgw_data_item_t **item);
EXPORT void mgw_data_item_release(mgw_data_item_t **item);
EXPORT void mgw_data_item_remove(mgw_data_item_t **item);

/* Gets Item type */
EXPORT enum mgw_data_type mgw_data_item_gettype(mgw_data_item_t *item);
EXPORT enum mgw_data_number_type mgw_data_item_numtype(mgw_data_item_t *item);
EXPORT const char *mgw_data_item_get_name(mgw_data_item_t *item);

/* Item set functions */
EXPORT void mgw_data_item_set_string(mgw_data_item_t **item, const char *val);
EXPORT void mgw_data_item_set_int(mgw_data_item_t **item, long long val);
EXPORT void mgw_data_item_set_double(mgw_data_item_t **item, double val);
EXPORT void mgw_data_item_set_bool(mgw_data_item_t **item, bool val);
EXPORT void mgw_data_item_set_obj(mgw_data_item_t **item, mgw_data_t *val);
EXPORT void mgw_data_item_set_array(mgw_data_item_t **item,
		mgw_data_array_t *val);

EXPORT void mgw_data_item_set_default_string(mgw_data_item_t **item,
		const char *val);
EXPORT void mgw_data_item_set_default_int(mgw_data_item_t **item, long long val);
EXPORT void mgw_data_item_set_default_double(mgw_data_item_t **item, double val);
EXPORT void mgw_data_item_set_default_bool(mgw_data_item_t **item, bool val);
EXPORT void mgw_data_item_set_default_obj(mgw_data_item_t **item,
		mgw_data_t *val);
EXPORT void mgw_data_item_set_default_array(mgw_data_item_t **item,
		mgw_data_array_t *val);

EXPORT void mgw_data_item_set_autoselect_string(mgw_data_item_t **item,
		const char *val);
EXPORT void mgw_data_item_set_autoselect_int(mgw_data_item_t **item,
		long long val);
EXPORT void mgw_data_item_set_autoselect_double(mgw_data_item_t **item,
		double val);
EXPORT void mgw_data_item_set_autoselect_bool(mgw_data_item_t **item, bool val);
EXPORT void mgw_data_item_set_autoselect_obj(mgw_data_item_t **item,
		mgw_data_t *val);
EXPORT void mgw_data_item_set_autoselect_array(mgw_data_item_t **item,
		mgw_data_array_t *val);

/* Item get functions */
EXPORT const char *mgw_data_item_get_string(mgw_data_item_t *item);
EXPORT long long mgw_data_item_get_int(mgw_data_item_t *item);
EXPORT double mgw_data_item_get_double(mgw_data_item_t *item);
EXPORT bool mgw_data_item_get_bool(mgw_data_item_t *item);
EXPORT mgw_data_t *mgw_data_item_get_obj(mgw_data_item_t *item);
EXPORT mgw_data_array_t *mgw_data_item_get_array(mgw_data_item_t *item);

EXPORT const char *mgw_data_item_get_default_string(mgw_data_item_t *item);
EXPORT long long mgw_data_item_get_default_int(mgw_data_item_t *item);
EXPORT double mgw_data_item_get_default_double(mgw_data_item_t *item);
EXPORT bool mgw_data_item_get_default_bool(mgw_data_item_t *item);
EXPORT mgw_data_t *mgw_data_item_get_default_obj(mgw_data_item_t *item);
EXPORT mgw_data_array_t *mgw_data_item_get_default_array(mgw_data_item_t *item);

EXPORT const char *mgw_data_item_get_autoselect_string(mgw_data_item_t *item);
EXPORT long long mgw_data_item_get_autoselect_int(mgw_data_item_t *item);
EXPORT double mgw_data_item_get_autoselect_double(mgw_data_item_t *item);
EXPORT bool mgw_data_item_get_autoselect_bool(mgw_data_item_t *item);
EXPORT mgw_data_t *mgw_data_item_get_autoselect_obj(mgw_data_item_t *item);
EXPORT mgw_data_array_t *mgw_data_item_get_autoselect_array(
		mgw_data_item_t *item);

static inline mgw_data_t *mgw_data_newref(mgw_data_t *data)
{
	if (data)
		mgw_data_addref(data);
	else
		data = mgw_data_create();

	return data;
}

#ifdef __cplusplus
}
#endif
