#ifndef _PLUGINS_FORMATS_MGW_FORMATS_H_
#define _PLUGINS_FORMATS_MGW_FORMATS_H_

#include "util/codec-def.h"
#include "util/mgw-data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MGW_FORMATS_MAJOR_VER   1
#define MGW_FORMATS_MINOR_VER   0
#define MGW_FORMATS_PATCH_VER   0

#define MGW_FORMAT_CUSTOM_IO	(0x01 << 0)
#define MGW_FORMAT_NO_BUFFER	(0x01 << 1)
#define MGW_FORMAT_NO_FILE		(0x01 << 2)
#define MGW_FORMAT_NO_BLOCK		(0x01 << 3)

#define MPEGTS_FIX_SIZE			188

struct mgw_format;
typedef struct mgw_format mgw_format_t;
typedef int (*proc_packet)(void *opaque, uint8_t *buf, int buf_size);

struct mgw_format_info {
    const char		*id;

	const char		*(*get_name)(void *type);
	void			*(*create)(mgw_data_t *settings, int flags,
							proc_packet write_packet, void *opaque);
	void			(*destroy)(void *data);
	bool			(*start)(void *data);
	void			(*stop)(void *data);
	size_t			(*send_packet)(void *data, struct encoder_packet *packet);

	/**< Options */
	mgw_data_t		*(*get_settings)(void* data);
	mgw_data_t		*(*get_default)(void);
	void			(*update)(void *data, mgw_data_t *settings);
	void			(*set_extra_data)(void *data, const uint8_t *extra_data, size_t size);
};


#ifdef __cplusplus
}
#endif
#endif  //_PLUGINS_FORMATS_MGW_FORMATS_H_