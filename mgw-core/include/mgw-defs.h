#ifndef _MGW_CORE_MGW_DEFS_H_
#define _MGW_CORE_MGW_DEFS_H_

/*
 * Increment if major breaking API changes, 8 bits
 */
#define LIBMGW_API_MAJOR_VER		1
/*
 * Increment if backward-compatible additions, 8 bits
 *
 * Reset to zero each major version
 */
#define LIBMGW_API_MINOR_VER		1
/*
 * Increment if backward-compatible bug fix, 16 bits
 *
 * Reset to zero each major or minor version
 */
#define LIBMGW_API_PATCH_VER		0

#define LIBMGW_API_SHA1				""

#define MGW_MAKE_VERSION_INT(major, minor, patch) \
				((major << 24) | (minor << 16) | patch)

#define MODULE_SUCCESS             0
#define MODULE_ERROR              -1
#define MODULE_FILE_NOT_FOUND     -2
#define MODULE_MISSING_EXPORTS    -3
#define MODULE_INCOMPATIBLE_VER   -4

#define MGW_OUTPUT_SUCCESS         0
#define MGW_OUTPUT_BAD_PATH       -1
#define MGW_OUTPUT_CONNECT_FAILED -2
#define MGW_OUTPUT_INVALID_STREAM -3
#define MGW_OUTPUT_ERROR          -4
#define MGW_OUTPUT_DISCONNECTED   -5
#define MGW_OUTPUT_UNSUPPORTED    -6
#define MGW_OUTPUT_NO_SPACE       -7

enum mgw_obj_type {
	MGW_OBJ_TYPE_INVALID,
	MGW_OBJ_TYPE_SOURCE,
	MGW_OBJ_TYPE_OUTPUT,
	MGW_OBJ_TYPE_FORMAT,
	MGW_OBJ_TYPE_ENCODER,
	MGW_OBJ_TYPE_SERVICE,
	MGW_OBJ_TYPE_DEVEICE,
};


#endif  //_MGW_CORE_MGW_DEFS_H_