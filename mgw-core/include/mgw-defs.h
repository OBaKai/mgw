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

/**< msb(8bits) is modules identity, lsb(16bits) is error code */
#define mgw_sys_err(x)		((0x01 << 24) | ((x) & 0x0000ffff))
#define mgw_mod_err(x)		((0x02 << 24) | ((x) & 0x0000ffff))
#define mgw_ser_err(x)		((0x03 << 24) | ((x) & 0x0000ffff))
#define mgw_dev_err(x)		((0x04 << 24) | ((x) & 0x0000ffff))
#define mgw_str_err(x)		((0x05 << 24) | ((x) & 0x0000ffff))
#define mgw_src_err(x)		((0x06 << 24) | ((x) & 0x0000ffff))
#define mgw_out_err(x)		((0x07 << 24) | ((x) & 0x0000ffff))
#define mgw_fmt_err(x)		((0x08 << 24) | ((x) & 0x0000ffff))

#define MODULE_SUCCESS             0
#define MODULE_ERROR              -1
#define MODULE_FILE_NOT_FOUND     -2
#define MODULE_MISSING_EXPORTS    -3
#define MODULE_INCOMPATIBLE_VER   -4

/**< Output stream result for mgw internal */
#define MGW_SUCCESS         0
#define MGW_BAD_PATH       -1
#define MGW_CONNECT_FAILED -2
#define MGW_INVALID_STREAM -3
#define MGW_ERROR          -4
#define MGW_DISCONNECTED   -5
#define MGW_UNSUPPORTED    -6
#define MGW_NO_SPACE       -7

/**< Output stream status for user */
#define MGW_OUTPUT_STATUS_STOPED			0
#define MGW_OUTPUT_STATUS_CONNECTING		1
#define MGW_OUTPUT_STATUS_RECONNECTING		2
#define MGW_OUTPUT_STATUS_CONNECTFAILED		3
#define MGW_OUTPUT_STATUS_STREAMING			4

/**< For user, those field must be set to 
 *   mgw_data_t for settings to start a stream output
 * */
#define	MGW_OUTPUT_FIELD_CHANNEL	"channel"
#define MGW_OUTPUT_FIELD_ID			"id"
#define MGW_OUTPUT_FIELD_PROTOCOL	"channel"
#define MGW_OUTPUT_FIELD_PATH		"path"
#define MGW_OUTPUT_FIELD_KEY		"key"
/** If inneed */
#define MGW_OUTPUT_FIELD_USERNAME	"username"
#define MGW_OUTPUT_FIELD_PASSWORD	"password"
#define MGW_OUTPUT_FIELD_DEST_IP	"dest_ip"	//The IP of stream address
#define MGW_OUTPUT_FIELD_NETIF_TYPE	"netif_type"
#define MGW_OUTPUT_FIELD_NETIF_NAME	"netif_name"

/** netif type */
#define MGW_OUTPUT_NETTYPE_IP		"ip"
#define MGW_OUTPUT_NETTYPE_NETCARD	"net_card"


enum mgw_obj_type {
	MGW_OBJ_TYPE_INVALID,
	MGW_OBJ_TYPE_SOURCE,
	MGW_OBJ_TYPE_OUTPUT,
	MGW_OBJ_TYPE_FORMAT,
	MGW_OBJ_TYPE_ENCODER,
	MGW_OBJ_TYPE_SERVICE,
	MGW_OBJ_TYPE_STREAM,
	MGW_OBJ_TYPE_DEVEICE,
};

enum mgw_err_status {
	MGW_ERR_SUCCESS	= 0,
	MGW_ERR_EPARAM,			/**< error parameter */
	MGW_ERR_INVALID_RES,	/**< Invalid resource */
	MGW_ERR_EXISTED,		/**< Resource already existed */
	MGW_ERR_ECREATE,
	MGW_ERR_ESTARTING,		/**< Start a resource failed */
};

#endif  //_MGW_CORE_MGW_DEFS_H_