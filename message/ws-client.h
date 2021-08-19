#ifndef __MESSAGE_WS_CLIENT_H__
#define __MESSAGE_WS_CLIENT_H__

#include <util/c99defs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	STATUS_SUCCESS				= 0,
	STATUS_PARAMER_INVALID		= -1,
	STATUS_LOOPING_FAILED		= -2,

	STATUS_PARSE_ERROR			= -3,
} ws_status;

typedef enum command_type {
	CMD_START_STREAM		= 0,
	CMD_REQ_PULLADDR		= 1,
}cmd_t;

typedef struct message_resp {
	int			status;
	void		*body;
	size_t		size;
}msg_resp_t;

typedef msg_resp_t (*write_cb)(void *, char *data, cmd_t cmd);

struct wsclient_info {
	const char *uri;
	const char *access_token;
	void *opaque;
	write_cb cb;
	uint32_t hb_interval;
	uint16_t port;
};

void *wsclient_create(struct wsclient_info *info);
int wsclient_start(void *data);
void wsclient_stop(void *data);
void wsclient_destroy(void *data);

/**< Mis */
bool wsclient_actived(void *data);
int wsclient_send(void *data, const char *buf, size_t size);

#ifdef __cplusplus
}
#endif
#endif  //__MESSAGE_WS_CLIENT_H__