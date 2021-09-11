#include "ws-client.h"
#include "message-def.h"
#include "util/tlog.h"
#include "util/base.h"
#include "util/bmem.h"
#include "util/dstr.h"
#include "util/c99defs.h"
#include "util/threading.h"
#include "libwebsockets.h"

#include "sys/types.h"
#include "sys/socket.h"
#include "arpa/inet.h"
#include "netdb.h"
#include "error.h"

#include <inttypes.h>

#define RX_BUFFER_SIZE		(2048+LWS_PRE)
#define SERVICE_TIMEOUT		1000	//ms
#define RECORD_NODE_MAX		30
#define RECORD_DEST_MAX		64
#define REPEATED_GAP_MAX	30	//s

/**< For filter the repeated message */
struct record_info {
	uint32_t	last_msg_no;
	uint32_t	last_ts;
};

struct message_record {
	bool				used;
	uint32_t			last_recv_ts;
	uint32_t			last_recv_total;
	struct dstr			dest_addr;
	struct record_info	rec_info[RECORD_NODE_MAX];
};

struct ws_client {
	uint16_t			retry_count;
	int					ssl_flag;
	uint32_t			message_no;
	uint32_t			hb_interval;	//ms
	uint64_t			last_check_hb;
	uint32_t			ip_val;
	struct in6_addr		v6;
	struct dstr 		proto, name, vhost, ip_str, path;
	struct dstr			peer_address;

	uint8_t 			*snd_buffer;
	size_t				snd_size;
	volatile long		send_req_cnt;
	volatile long		send_do_cnt;

	pthread_t			loop_thread;
	pthread_mutex_t		mutex;
	volatile bool		looping;
	volatile bool		actived;
	volatile bool		snd_req;
	os_event_t			*stop_event;
	os_sem_t			*snd_sem;

	struct message_record	record[RECORD_DEST_MAX];
	struct wsclient_info	*info;

	lws_sorted_usec_list_t	sul;
	struct lws				*wsi;
	struct lws_context		*context;
};

static int lws_callback(struct lws *wsi,
			enum lws_callback_reasons reason,
		    void *user, void *in, size_t len);

static inline bool stopping(struct ws_client *client)
{
	return os_event_try(client->stop_event) != EAGAIN;
}

static inline bool sending(struct ws_client *client)
{
	return os_atomic_load_bool(&client->snd_req);
}

static inline bool looping(struct ws_client *client)
{
	return os_atomic_load_bool(&client->looping);
}

static inline bool actived(struct ws_client *client)
{
	return os_atomic_load_bool(&client->actived);
}

static inline bool time_elapse_ms(uint64_t elapse)
{
	static uint64_t last_time = 0;
	struct timeval tv = {};
	gettimeofday(&tv, NULL);
	uint64_t cur_time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	if (abs(cur_time - last_time) >= elapse) {
		last_time = cur_time;
		return true;
	} else {
		return false;
	}
}

void wsclient_destroy(void *data)
{
	struct ws_client *client = data;
	if (!client)
		return;

	if (!stopping(client)) {
		os_event_signal(client->stop_event);
		pthread_join(client->loop_thread, NULL);
	}

	dstr_free(&client->proto);
	dstr_free(&client->name);
	dstr_free(&client->vhost);
	dstr_free(&client->ip_str);
	dstr_free(&client->path);
	bfree(client->snd_buffer);
	os_event_destroy(client->stop_event);
	pthread_mutex_destroy(&client->mutex);
	os_sem_destroy(client->snd_sem);

	lws_context_destroy(client->context);
	bfree(client);
}

static inline void send_message(struct ws_client *client,
			void *body, size_t size, submsg_type_t type)
{
	common_head_t	common = {};
	common.magic		= htonl(MAGIC_CODE);
	common.msg_ver		= MESSAGE_VER;
	common.msg_headlen	= COMMON_HEAD_SIZE;
	common.msg_no		= (SUBMSG_TYPE_PONG == type ||
						SUBMSG_TYPE_PING == type) ? 0 : htonl(client->message_no++);
	common.mask_code	= 0;
	common.text_len		= htons(size);

	common.msg_attr.bit_val.msg_type	= type;
	common.msg_attr.bit_val.mask_flag	= 0;
	common.msg_attr.bit_val.ack_flag	= 0;
	common.msg_attr.bit_val.ack_req		= SUBMSG_TYPE_PONG == type ? 0 : 1;
	common.msg_attr.bit_val.split_msg	= 0;

	tlog(TLOG_INFO, "Waiting send sem.......");
	if (os_sem_wait(client->snd_sem) == 0) {
		pthread_mutex_lock(&client->mutex);
		uint8_t *payload = client->snd_buffer + LWS_PRE;
		memcpy(payload, &common, COMMON_HEAD_SIZE);
		client->snd_size = COMMON_HEAD_SIZE;
		if (!!body) {
			memcpy(payload+COMMON_HEAD_SIZE, body, size);
			client->snd_size += size;
		}
		tlog(MGW_LOG_INFO, "Send message:%d head size:%lu, body size:%ld", type, COMMON_HEAD_SIZE, size);
		int ret = lws_callback_on_writable(client->wsi);
		os_atomic_inc_long(&client->send_req_cnt);
		tlog(MGW_LOG_INFO, "Sending message ret:%d, err:%s, req_cnt:%ld", ret,
						strerror(errno), os_atomic_load_long(&client->send_req_cnt));
		os_atomic_set_bool(&client->snd_req, true);
		pthread_mutex_unlock(&client->mutex);
	}
}

static inline uint8_t char_to_hex(uint8_t c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'A' && c <= 'Z')
		return c - 'A' + 0xa;
	else if (c >= 'a' && c <= 'z')
		return c - 'a' + 0xa;
	else
		return -1;
}

static bool request_authorization(struct ws_client *client)
{
	uint8_t access_token_length = strlen(client->info->access_token);
	if (access_token_length != 40) {
		tlog(TLOG_ERROR, "access token length: %d invalid!\n", access_token_length);
		return false;
	}

	authen_req_t req_body = {};
	req_body.authen_type = AUTHEN_TYPE_REQ;
	for (int i = 0, j = 0; i < access_token_length; i += 2, j++) {
		req_body.access_token[j] |= char_to_hex(client->info->access_token[i]) << 4;
		req_body.access_token[j] |= char_to_hex(client->info->access_token[i+1]);
	}
	send_message(client, (void*)&req_body, sizeof(req_body), SUBMSG_TYPE_AUTHEN);
	return true;
}

static inline uint32_t ip_str_to_value(const char *ip)
{
	struct in_addr s = {};
	inet_pton(AF_INET, ip, &s);
	return s.s_addr;
}

static void send_ping(struct ws_client *client)
{
	// char ip[64] = {};
	ping_body_t	body = {};
	body.port	= htons(client->info->port);
	body.ip		= htonl(client->ip_val);//ip_str_to_value(client->ip.array);
	tlog(MGW_LOG_INFO, "Sending ping to %d, port:%d", body.ip, client->info->port);
	send_message(client, (void*)&body, sizeof(body), SUBMSG_TYPE_PING);
}

static inline void send_pong(struct ws_client *client)
{
	send_message(client, NULL, 0, SUBMSG_TYPE_PONG);
}

static struct lws_protocols protocols[] = {
	{
		.name		= "ws",
		.callback	= lws_callback,
		.per_session_data_size = sizeof(struct ws_client),
		.rx_buffer_size = RX_BUFFER_SIZE
	},
	{
		.name		= NULL,
		.callback	= NULL,
		.per_session_data_size = 0,
		.rx_buffer_size = 0
	}
};

static const uint32_t backoff_ms[] = { 1000 };

static const lws_retry_bo_t retry = {
	.retry_ms_table			= backoff_ms,
	.retry_ms_table_count	= LWS_ARRAY_SIZE(backoff_ms),
	.conceal_count			= LWS_RETRY_CONCEAL_ALWAYS,

	.secs_since_valid_ping		= 400,  /* force PINGs after secs idle */
	.secs_since_valid_hangup	= 15*60, /* hangup after secs idle */

	.jitter_percent			= 0,
};

static void try_connect(struct lws_sorted_usec_list *sul)
{
	struct ws_client *client = lws_container_of(sul, struct ws_client, sul);
	struct lws_client_connect_info info = {};

	info.context		= client->context;
	info.address		= client->vhost.array;
	info.port			= client->info->port;
	info.ssl_connection = client->ssl_flag;
	info.path			= client->path.array;
	info.host			= client->vhost.array;
	info.origin			= client->ip_str.array;
	info.protocol		= protocols[0].name;
	info.userdata		= client;
	info.pwsi			= &client->wsi;
	info.retry_and_idle_policy = &retry;

	if (!(client->wsi = lws_client_connect_via_info(&info))) {
		tlog(TLOG_WARN, "Tried to connect server failed, retry later...\n");
		if (lws_retry_sul_schedule(client->context, 0, sul, &retry,
							try_connect, &client->retry_count)) {
			tlog(TLOG_ERROR, "connection attempts exhausted, retry count:%d\n", client->retry_count);
			// os_event_signal(client->stop_event);
		}
	}
	tlog(TLOG_INFO, "Try to connect proto:%s, name:%s, host:%s, "
					"ip:%s, port:%d, path:%s, flag:%d, retry:%d\n",
					info.protocol, info.address, info.host, info.origin,
					info.port, info.path, info.ssl_connection, client->retry_count);

	lws_service_adjust_timeout(client->context, 1000*10, lws_get_tsi(client->wsi));
}

static bool repeated_message(struct ws_client *client, text_recv_body_t *text)
{
	int msg_no = ntohl(text->com_body.com_head.msg_no);
	time_t cur_ts = time(NULL);
	int i = 0, pos = msg_no % RECORD_NODE_MAX;
	bool new_dest = true;

	for (; i < RECORD_DEST_MAX; i++) {
		if (!client->record[i].used)
			break;

		if (!dstr_cmp(&client->record->dest_addr, (const char *)text->com_body.text_head.local_addr)) {
			new_dest = false;
			if (client->record[i].rec_info[pos].last_msg_no == msg_no &&
				abs(client->record[pos].last_recv_ts - cur_ts) < REPEATED_GAP_MAX) {
				tlog(MGW_LOG_INFO, "Is a repeated message, Drop it!");
				return true;
			} else {
				/**< record it */
				client->record[i].rec_info[pos].last_msg_no = msg_no;
				client->record[i].rec_info[pos].last_ts = cur_ts;
				tlog(MGW_LOG_INFO, "record a new message!");
				return false;
			}
		}
	}

	if (new_dest && i < RECORD_DEST_MAX) {
		dstr_copy(&client->record[i].dest_addr, (const char *)text->com_body.text_head.local_addr);
		client->record[i].rec_info[pos].last_msg_no = msg_no;
		client->record[i].rec_info[pos].last_ts = cur_ts;
		client->record[i].last_recv_total++;
		client->record[i].used = true;
	}
	return false;
}

static int response_message(struct ws_client *client,
				msg_resp_t *resp, text_com_body_t *com_body)
{
	text_resp_body_t resp_text = {};

	/**< Response common head */
	memcpy(&resp_text.com_body, com_body, sizeof(text_com_body_t));
	resp_text.resp_body.status = resp->size;
	/**< Response body */
	tlog(MGW_LOG_INFO, "Waiting send sem.......");
	if (os_sem_wait(client->snd_sem) == 0) {
		pthread_mutex_lock(&client->mutex);
		// if (!sending(client)) {
			uint8_t *payload = client->snd_buffer + LWS_PRE;
			memcpy(payload, &resp_text, sizeof(text_resp_body_t));
			if (!!resp->body)
				memcpy(payload+COMMON_HEAD_SIZE, resp->body, resp->size);
			lws_callback_on_writable(client->wsi);
			os_atomic_set_bool(&client->snd_req, true);
			os_atomic_inc_long(&client->send_req_cnt);
			tlog(MGW_LOG_INFO, "Reponse message, req_cnt:%ld", os_atomic_load_long(&client->send_req_cnt));
		// }
		pthread_mutex_unlock(&client->mutex);
	}

	return 0;
}

static void receive_message(struct ws_client *client,
			void *in, size_t len, submsg_type_t msg_type)
{
	common_head_t *com_head = in;
	switch (msg_type) {
		case SUBMSG_TYPE_NOP: {
			break;
		}
		case SUBMSG_TYPE_PING: {
			tlog(MGW_LOG_INFO, "Receiving ping........................");
			send_pong(client);
			break;
		}
		case SUBMSG_TYPE_PONG: {
			pong_body_t *pong = in + com_head->msg_headlen;
			if (!pong->authen) {
				request_authorization(client);
			}
			tlog(MGW_LOG_INFO, "Receiving pong........................");
			uint32_t ip = ntohl(pong->ip);
			uint16_t port = ntohs(pong->port);
			if (client->info->port != port || client->ip_val != ip) {
				tlog(TLOG_INFO, "Receive pong message, original ip:%d, port:%d; new ip:%d, port:%d\n",
									client->ip_val, client->info->port, ip, port);
				client->ip_val = ip;
				client->info->port = port;
			}
			break;
		}
		case SUBMSG_TYPE_AUTHEN: {
			authen_res_t *authen = in + com_head->msg_headlen;
			tlog(MGW_LOG_INFO, "Received authen message type:%d", authen->authen_type);
			if (AUTHEN_TYPE_UNAUTHOR == authen->authen_type) {
				tlog(TLOG_ERROR, "authorization result is unauthorization, retry later...\n");
				request_authorization(client);
			} else if (AUTHEN_TYPE_RES == authen->authen_type) {
				tlog(MGW_LOG_INFO, "Server response address:%s", authen->address);
				dstr_copy(&client->peer_address, (const char *)authen->address);
			}
			break;
		}
		case SUBMSG_TYPE_TEXT: {
			text_recv_body_t *text = in;
			if (!repeated_message(client, text)) {
				msg_resp_t resp = client->info->cb(client->info->opaque, text->recv_body.body,
											text->com_body.com_head.text_len, text->com_body.cmd_type);

				if (text->com_body.com_head.msg_attr.bit_val.ack_req)
					response_message(client, &resp, &text->com_body);
				if (resp.body)
					bfree(resp.body);
			}
			break;
		}
		default: break;
	}
}

static int lws_callback(struct lws *wsi,
						enum lws_callback_reasons reason,
		    			void *user, void *in, size_t len)
{
	struct ws_client *client = user;

	switch (reason) {
		case LWS_CALLBACK_PROTOCOL_INIT: {

			break;
		}
		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
			tlog(TLOG_ERROR, "ws connection error occur, retry later...\n");
			os_atomic_set_bool(&client->actived, false);
			goto retry;
		}
		case LWS_CALLBACK_CLIENT_ESTABLISHED: {
			tlog(MGW_LOG_INFO, "ws connected\n");
			request_authorization(client);
			os_atomic_set_bool(&client->actived, true);
			break;
		}
		case LWS_CALLBACK_CLIENT_RECEIVE: {
			common_head_t *com_head = in;
			if (MAGIC_CODE == ntohl(com_head->magic))
				receive_message(client, in, len, com_head->msg_attr.bit_val.msg_type);

			break;
		}
		case LWS_CALLBACK_CLIENT_WRITEABLE: {
			pthread_mutex_lock(&client->mutex);
			// if (os_atomic_load_bool(&client->snd_req)) {
				lwsl_hexdump_err(client->snd_buffer+LWS_PRE, client->snd_size);
				if (client->snd_size > 0) {
					lws_write(wsi, client->snd_buffer+LWS_PRE, client->snd_size, LWS_WRITE_BINARY);
					client->snd_size = 0;
					os_sem_post(client->snd_sem);
					os_atomic_set_bool(&client->snd_req, false);
					os_atomic_inc_long(&client->send_do_cnt);
					tlog(MGW_LOG_INFO, "Post callback sending sem connote writable, send_cnt:%ld", os_atomic_load_long(&client->send_do_cnt));
				}
			// }
			pthread_mutex_unlock(&client->mutex);
			break;
		}
		case LWS_CALLBACK_CLIENT_CLOSED: {
			tlog(TLOG_INFO, "ws connection closed, retry later...\n");
			os_atomic_set_bool(&client->actived, false);
			goto retry;
		}
		default: break;
	}

	return lws_callback_http_dummy(wsi, reason, user, in, len);

retry:
	if (lws_retry_sul_schedule_retry_wsi(wsi, &client->sul,
					try_connect, &client->retry_count)) {
		tlog(TLOG_ERROR, "connection attempts exhausted, retry count:%d\n", client->retry_count);
		// os_event_signal(client->stop_event);
	}

	return 0;
}

static bool wsc_get_addr_info(struct ws_client *client,
		struct dstr *host, uint16_t port, int *socket_error)
{
	char *hostname;
    int ret = true;
    if (host->array[host->len] || host->array[0] == '[')
    {
        int v6 = host->array[0] == '[';
        hostname = malloc(host->len+1 - v6 * 2);
        memcpy(hostname, host->array + v6, host->len - v6 * 2);
        hostname[host->len - v6 * 2] = '\0';
    }
    else
    {
        hostname = host->array;
    }

    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *ptr = NULL;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portStr[8];
    sprintf(portStr, "%d", port);

    int err = getaddrinfo(hostname, portStr, &hints, &result);

    if (err)
    {
#ifndef _WIN32
#define gai_strerrorA gai_strerror
#endif
        tlog(TLOG_ERROR, "Could not resolve %s: %s (%d)", hostname, gai_strerrorA(errno), errno);
        *socket_error = errno;
        ret = false;
        goto finish;
    }

	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
		if (ptr->ai_family == AF_INET) {
			/**< IPv4 */
			char ip_buf[64] = {};
			struct in_addr v4 = {((struct sockaddr_in *)(ptr->ai_addr))->sin_addr.s_addr};
			client->ip_val = v4.s_addr;
			inet_ntop(AF_INET, &v4, ip_buf, 64);
			dstr_copy(&client->ip_str, ip_buf);
		} else if (ptr->ai_family == AF_INET6) {
			/**< IPv6 */
			char ip_buf[64] = {};
			struct in6_addr v6 = ((struct sockaddr_in6*)(ptr->ai_addr))->sin6_addr;
			client->v6 = v6;
			inet_ntop(AF_INET6, &v6, ip_buf, 64);
			dstr_copy(&client->ip_str, ip_buf);
		}
	}

    freeaddrinfo(result);

finish:
    if (hostname != host->array)
        free(hostname);
    return ret;
}

static inline bool paramer_valid(struct wsclient_info *wsinfo)
{
	return !!wsinfo && !!wsinfo->uri && !!wsinfo->cb && !!wsinfo->access_token;
}

void *wsclient_create(struct wsclient_info *wsinfo)
{
	if (!paramer_valid(wsinfo)) {
		tlog(TLOG_ERROR, "Tried to create wsclient but paramer error!\n");
		return NULL;
	}

	struct lws_context_creation_info info = {};
	struct ws_client *client = bzalloc(sizeof(struct ws_client));

	if (0 != os_event_init(&client->stop_event, OS_EVENT_TYPE_MANUAL))
		goto error;
	if (0 != pthread_mutex_init(&client->mutex, NULL))
		goto error;
	if (0!= os_sem_init(&client->snd_sem, 0))
		goto error;

	client->info = wsinfo;
	client->snd_buffer = bzalloc(RX_BUFFER_SIZE);

	/**< parse uri */
	int sock_err = 0, port = 0;
	char uri[1024] = {};
	strncpy(uri, wsinfo->uri, sizeof(uri) - 0);
	const char *proto = NULL, *vhost = NULL, *path = NULL;

	if (lws_parse_uri(uri, &proto, &vhost, &port, &path)) {
		tlog(TLOG_ERROR, "Coundn't parse uri:%s\n", wsinfo->uri);
	}
	if (port != 0)
		client->info->port = port;

	dstr_copy(&client->proto, proto);
	dstr_cat_ch(&client->path, '/');
	dstr_cat(&client->path, path);
	dstr_copy(&client->vhost, vhost);
	if (!dstr_is_empty(&client->vhost) && !wsc_get_addr_info(client,
					&client->vhost, client->info->port, &sock_err)) {
		tlog(TLOG_ERROR, "Get host ip failed, error:%s\n", strerror(sock_err));
	}
	if (0 == dstr_ncmp(&client->proto, "wss", 3))
		client->ssl_flag |= LCCSCF_USE_SSL;

	info.protocols = protocols;
	info.port = client->info->port;
	info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

	client->context = lws_create_context(&info);
	if (!client->context) {
		tlog(TLOG_ERROR, "Tried to create ws context failed!");
		goto error;
	}

	tlog(MGW_LOG_INFO, "Post sending sem connote writable....");
	os_sem_post(client->snd_sem);
	return client;

error:
	wsclient_destroy(client);
	return NULL;
}

static void *loop_thread(void *arg)
{
	struct ws_client *client = arg;

	os_set_thread_name("websocket-client: loop thread");
	while (looping(client)) {
		if (stopping(client))
			break;

		if (client->context && actived(client))
			// lws_service(client->context, 0);
			lws_service_tsi(client->context, -1, lws_get_tsi(client->wsi));
		else if (client->context)
			lws_service(client->context, 0);

		if (actived(client) && !sending(client) &&
			time_elapse_ms(1000 * client->info->hb_interval))
			send_ping(client);

		usleep(1000*1000*2);
	}

	tlog(TLOG_WARN, "Looping thread exit, loop:%d\n", looping(client));
	if (looping(client))
		pthread_detach(client->loop_thread);

	os_event_reset(client->stop_event);
	os_atomic_set_bool(&client->looping, false);
	os_atomic_set_bool(&client->actived, false);
	return NULL;
}

int wsclient_start(void *data)
{
	struct ws_client *client = data;
	if (!client)
		return STATUS_PARAMER_INVALID;

	lws_sul_schedule(client->context, 0, &client->sul, try_connect, 1);
	os_atomic_set_bool(&client->looping, true);
	if (0 != pthread_create(&client->loop_thread, NULL, loop_thread, client)) {
		tlog(TLOG_ERROR, "Tried to create ws client loop thread failed!\n");
		return STATUS_LOOPING_FAILED;
	}

	return 0;
}

void wsclient_stop(void *data)
{
	struct ws_client *client = data;
	if (!client)
		return;

	if (stopping(client))
		return;
	else
		os_event_signal(client->stop_event);
}

/**< Mis */
int wsclient_send(void *data, const char *buf, size_t size)
{
	struct ws_client *client = data;
	if (!client || !buf || !size)
		return STATUS_PARAMER_INVALID;

	if (os_sem_wait(client->snd_sem) == 0) {
		if (actived(client)/* && !sending(client)*/) {
			pthread_mutex_lock(&client->mutex);
			uint8_t *payload = client->snd_buffer + LWS_PRE;
			memcpy(payload, buf, size);
			client->snd_size = size;
			lws_callback_on_writable(client->wsi);
			os_atomic_set_bool(&client->snd_req, true);
			os_atomic_inc_long(&client->send_req_cnt);
			tlog(MGW_LOG_INFO, "wsclient sending, req_cnt:%ld",
						os_atomic_load_long(&client->send_req_cnt));
			pthread_mutex_unlock(&client->mutex);
		}
	}

	return STATUS_SUCCESS;
}

bool wsclient_actived(void *data)
{
	struct ws_client *client = data;
	if (!client)
		return false;

	return actived(client);
}