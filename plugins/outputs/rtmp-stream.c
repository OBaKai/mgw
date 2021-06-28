#include <signal.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include "mgw-internal.h"
#include "mgw-outputs.h"

#include "util/base.h"
#include "util/dstr.h"
#include "util/platform.h"

#include "formats/flv-mux.h"
#include "librtmp/log.h"
#include "librtmp/rtmp.h"

#undef  MODULE_NAME
#define MODULE_NAME     "rtmp_stream"

#define NETIF_DEF       0
#define NETIF_NETCARD   1
#define NETIF_IP        2
#define NETIF_MTU_DEF   1350
#define NETIF_TYPE_DEF  "default"
#define NETIF_NAME_DEF  ""

pthread_once_t rtmp_context_once = PTHREAD_ONCE_INIT;

struct rtmp_stream {
    mgw_output_t    *output;

    bool            new_socket_loop;
    bool            sent_headers;
    uint64_t        start_time, rct_time, stop_time;
    uint64_t        total_bytes_sent;
    uint64_t        audio_drop_frames, video_drop_frames;
    uint64_t        start_dts_offset;

    volatile bool   connecting;
    pthread_t       connect_thread;

    volatile bool    active;
	volatile bool    disconnected;
	pthread_t        send_thread;

    os_sem_t         *send_sem;
	os_event_t       *stop_event;

    uint32_t        netif_mtu;
    struct dstr     netif_type, netif_name;

    /* use by rtmp stream internal */
    uint16_t        port;
    struct dstr     path, key, dest_ip;
    struct dstr     username, password;
    struct dstr     encoder_name;

    RTMP            rtmp;

    int             max_shutdown_time_sec;
};

static inline bool stopping(struct rtmp_stream *stream)
{
	return os_event_try(stream->stop_event) != EAGAIN;
}

static inline bool connecting(struct rtmp_stream *stream)
{
	return os_atomic_load_bool(&stream->connecting);
}

static inline bool active(struct rtmp_stream *stream)
{
	return os_atomic_load_bool(&stream->active);
}

static inline bool disconnected(struct rtmp_stream *stream)
{
	return os_atomic_load_bool(&stream->disconnected);
}

static inline bool stream_valid(void *data)
{
    struct rtmp_stream *stream = data;
    return !!data && !!(&stream->output);
}

static void log_rtmp(int level, const char *fmt, va_list args)
{
    if (level > RTMP_LOGWARNING)
		return;
	blogva(MGW_LOG_INFO, fmt, args);
}

static void rtmp_stream_init_once(void)
{
    RTMP_LogSetCallback(log_rtmp);
	RTMP_LogSetLevel(RTMP_LOGWARNING);
    (void)signal(SIGPIPE, SIG_IGN);
    blog(MGW_LOG_INFO, "rtmp stream initialize once");
}

static void rtmp_stream_get_default(mgw_data_t *setting)
{
    if (!setting)
        return;

    mgw_data_set_int(setting, "netif_mtu", NETIF_MTU_DEF);
    mgw_data_set_string(setting, "netif_type", NETIF_TYPE_DEF);
    mgw_data_set_string(setting, "netif_name", NETIF_NAME_DEF);
}

static bool rtmp_stream_get_settings(void *data, mgw_data_t *settings)
{
    struct rtmp_stream *stream = data;
    if (!stream_valid(data))
        return false;

    mgw_data_set_int(settings, "drop_frames", 
            stream->audio_drop_frames + stream->video_drop_frames);
    mgw_data_set_int(settings, "start_time", stream->start_time);
    mgw_data_set_int(settings, "stop_time", stream->stop_time);

    mgw_data_set_int(settings, "netif_mtu", stream->netif_mtu);
    mgw_data_set_string(settings, "netif_type", stream->netif_type.array);
    mgw_data_set_string(settings, "netif_name", stream->netif_name.array);

    mgw_data_set_int(settings, "port", stream->port);
    mgw_data_set_string(settings, "path", stream->path.array);
    mgw_data_set_string(settings, "key", stream->key.array);
    mgw_data_set_string(settings, "dest_ip", stream->dest_ip.array);
    mgw_data_set_string(settings, "username", stream->username.array);
    mgw_data_set_string(settings, "password", stream->password.array);

    return true;
}

static const char *rtmp_stream_get_name(void *data)
{
    UNUSED_PARAMETER(data);
    return MODULE_NAME;
}

static void rtmp_stream_destroy(void *data)
{
    struct rtmp_stream *stream = data;
    /* stop connecting */
    if (stopping(stream) || !connecting(stream)) {
        pthread_join(stream->send_thread, NULL);
    } else if (connecting(stream) || active(stream)) {
        if (stream->connecting)
            pthread_join(stream->connect_thread, NULL);

        stream->stop_time = (uint64_t)time(NULL);
        os_event_signal(stream->stop_event);

        if(active(stream)) {
            os_sem_post(stream->send_sem);
            pthread_join(stream->send_thread, NULL);
        }
    }

    dstr_free(&stream->path);
    dstr_free(&stream->key);
    dstr_free(&stream->dest_ip);
    dstr_free(&stream->username);
    dstr_free(&stream->password);
    dstr_free(&stream->netif_name);

    os_event_destroy(stream->stop_event);
	os_sem_destroy(stream->send_sem);
    bfree(stream);
}

static void *rtmp_stream_create(mgw_data_t *setting, mgw_output_t *output)
{
    struct rtmp_stream *stream = bzalloc(sizeof(struct rtmp_stream));
    stream->output = output;

    pthread_once(&rtmp_context_once, rtmp_stream_init_once);
    RTMP_Init(&stream->rtmp);

    if (os_event_init(&stream->stop_event, OS_EVENT_TYPE_MANUAL) != 0)
		goto rtmp_fail;
    if (os_sem_init(&stream->send_sem, 0) != 0)
        goto rtmp_fail;

    //UNUSED_PARAMETER(setting);
    return stream;

rtmp_fail:
    rtmp_stream_destroy(stream);
    return NULL;
}

static bool netif_str_to_addr(struct sockaddr_storage *out, int *addr_len,
                       const char *addr)
{
    bool ipv6;
 
    memset(out, 0, sizeof(*out));
    *addr_len = 0;
 
    if (!addr)
        return false;
 
    ipv6 = (strchr(addr, ':') != NULL);
    out->ss_family = ipv6 ? AF_INET6 : AF_INET;
    *addr_len = sizeof(*out);
 
    struct sockaddr_in *sin = (struct sockaddr_in *)out;
    if (inet_pton(out->ss_family, addr, &sin->sin_addr))
    {
        *addr_len = ipv6 ?
                    sizeof(struct sockaddr_in6) :
                    sizeof(struct sockaddr_in);
        return true;
    }
 
    return false;
}

static void get_stream_code(struct dstr *url, struct dstr *code)
{
    char *p = url->array+strlen(url->array) - 1;
    while(*(p--) != '/');
    p++;
    *p = '\0';
    p++;
    dstr_copy(code, p);
    url->len = strlen(url->array);
}

static inline void set_rtmp_str(AVal *val, const char *str)
{
	bool valid  = (str && *str);
	val->av_val = valid ? (char*)str       : NULL;
	val->av_len = valid ? (int)strlen(str) : 0;
}

static inline void set_rtmp_dstr(AVal *val, struct dstr *str)
{
	bool valid  = !dstr_is_empty(str);
	val->av_val = valid ? str->array    : NULL;
	val->av_len = valid ? (int)str->len : 0;
}

static bool discard_recv_data(struct rtmp_stream *stream, size_t size)
{
    RTMP *rtmp = (RTMP*)&stream->rtmp;
    uint8_t buf[512];
    size_t ret;
 
    do
    {
        size_t bytes = size > 512 ? 512 : size;
        size -= bytes;
        ret = recv(rtmp->m_sb.sb_socket, buf, bytes, 0);
        if (ret <= 0)
        {
            int error = errno;
            blog(MGW_LOG_ERROR, "rtmp socket recv error: %d (%d bytes)", error, (int)size);
            return false;
        }
    }
    while (size > 0);
    return true;
}

static int send_packet(struct rtmp_stream *stream,
		struct encoder_packet *packet, bool is_header, size_t idx)
{
	uint8_t *data;
	size_t  size;
	int     recv_size = 0;
	int     ret = 0;

	uint64_t tick = packet->pts / 1000;

	if (!stream->start_dts_offset && !is_header) {
		if (ENCODER_VIDEO == packet->type)
			stream->start_dts_offset = tick;
		else
			return 0;
	}

	if (!stream->new_socket_loop) {
		ret = ioctl(stream->rtmp.m_sb.sb_socket, FIONREAD, &recv_size);
		if (ret >= 0 && recv_size > 0) {
			if (!discard_recv_data(stream, (size_t)recv_size)) {
                ret = -1;
                goto error;
            }
		}
	}

	packet->dts = packet->pts = tick - stream->start_dts_offset;

	flv_packet_mux(packet, is_header ? 0 : stream->start_dts_offset,
			&data, &size, is_header);

	ret = RTMP_Write(&stream->rtmp, (char*)data, (int)size, (int)idx);
	bfree(data);

	stream->total_bytes_sent += size;
error:
    if (is_header)
		bfree(packet->data);
	return ret;
}

static bool send_meta_data(struct rtmp_stream *stream, size_t idx)
{
	uint8_t *meta_data;
	size_t  meta_data_size;
	bool    success = true;

	success = flv_meta_data(stream->output->get_encoder_settings(stream->output),
					&meta_data, &meta_data_size, false, idx);

	if (success) {
		success = RTMP_Write(&stream->rtmp, (char*)meta_data,
				(int)meta_data_size, (int)idx) >= 0;
		bfree(meta_data);
	}

	return success;
}

static bool send_audio_header(struct rtmp_stream *stream, size_t idx,
		bool *next)
{
	mgw_output_t  *context  = stream->output;
	uint8_t       *header;

	struct encoder_packet packet   = {
		.type         = ENCODER_AUDIO,
		.timebase_den = 1
	};

	//must be AudioSpecificConfig -- aac
	packet.size = context->get_audio_header(context, &header);
	packet.data = bmemdup(header, packet.size);
	return send_packet(stream, &packet, true, idx) >= 0;
}

static bool send_video_header(struct rtmp_stream *stream)
{
	mgw_output_t  *context  = stream->output;
	uint8_t       *header;

	struct encoder_packet packet   = {
		.type         = ENCODER_VIDEO,
		.timebase_den = 1,
		.keyframe     = true
	};

	// must be AVCDecoderConfigurationRecord -- avc
	packet.size = context->get_video_header(context, &header);
	packet.data = bmemdup(header, packet.size);
	return send_packet(stream, &packet, true, 0) >= 0;
}

static inline bool send_headers(struct rtmp_stream *stream)
{
	stream->sent_headers = true;
	size_t i = 0;
	bool next = true;

	if (!send_audio_header(stream, i++, &next))
		return false;
	if (!send_video_header(stream))
		return false;

	return true;
}

static void *send_thread(void *data)
{
	struct rtmp_stream *stream = data;
	struct encoder_packet packet;

	os_set_thread_name("rtmp-stream: send_thread");
	blog(MGW_LOG_INFO, "rtmp-stream thread running!");
    while(/*os_sem_wait(stream->send_sem) == 0*/os_atomic_load_bool(&stream->active)) {
		if (stopping(stream))
			break;

        usleep(10 * 1000);
		if (!stream->output->get_next_encoder_packet(stream->output, &packet))
			continue;

		if (!stream->sent_headers) {
			if (!send_headers(stream)) {
				os_atomic_set_bool(&stream->disconnected, true);
				break;
			}
		}

		if (send_packet(stream, &packet, false, packet.track_idx) < 0) {
			os_atomic_set_bool(&stream->disconnected, true);
			break;
		}
		bfree(packet.data);
	}

	bfree(packet.data);
	if (disconnected(stream))
		blog(MGW_LOG_INFO, "Disconnected from %s", stream->path.array);
	else
		blog(MGW_LOG_INFO, "User stopped the stream");

	stream->output->last_error_status = stream->rtmp.last_error_code;
	RTMP_Close(&stream->rtmp);
	if (!stopping(stream)) {
		pthread_detach(stream->send_thread);
		stream->output->signal_stop(stream->output, MGW_OUTPUT_DISCONNECTED);
	}

	stream->sent_headers = false;
	os_event_reset(stream->stop_event);
	os_atomic_set_bool(&stream->active, false);
	return NULL;
}

static inline bool reset_semaphore(struct rtmp_stream *stream)
{
	os_sem_destroy(stream->send_sem);
	return os_sem_init(&stream->send_sem, 0) == 0;
}

static int init_send(struct rtmp_stream *stream)
{
    if (!send_meta_data(stream, 0)) {
        blog(MGW_LOG_ERROR, "Disconnected while attempting to connect to server!");
        stream->output->last_error_status = stream->rtmp.last_error_code;
        return MGW_OUTPUT_DISCONNECTED;
    }

    reset_semaphore(stream);
    os_atomic_set_bool(&stream->active, true);
    if (pthread_create(&stream->send_thread, NULL, send_thread, stream) != 0) {
        RTMP_Close(&stream->rtmp);
        blog(MGW_LOG_ERROR, "Failed to create send thread!");
        os_atomic_set_bool(&stream->active, false);
        return MGW_OUTPUT_ERROR;
    }

    return MGW_OUTPUT_SUCCESS;
}

static bool init_connect(struct rtmp_stream *stream)
{
	mgw_data_t *settings = NULL;

	if (stopping(stream)) {
		pthread_join(stream->send_thread, NULL);
	}
    os_atomic_set_bool(&stream->disconnected, false);

    mgw_data_t *output_settings = stream->output->context.settings;
    const char *path = mgw_data_get_string(output_settings, "path");
    const char *key = mgw_data_get_string(output_settings, "key");

	const char *username = mgw_data_get_string(output_settings, "username");
	const char *password = mgw_data_get_string(output_settings, "password");

	settings = stream->output->get_encoder_settings(stream->output);
    if (path && key) {
        dstr_copy(&stream->path,        path);
	    dstr_copy(&stream->key,         key);
    }
	if (username && password) {
		dstr_copy(&stream->username, username);
		dstr_copy(&stream->password, password);
	}
	/*
    dstr_copy(&stream->dest_ip,     mgw_data_get_string(settings, "dest_ip"));
    dstr_copy(&stream->netif_type,  mgw_data_get_string(settings, "netif_type"));
    dstr_copy(&stream->netif_name,  mgw_data_get_string(settings, "netif_name"));
    */

    if (dstr_is_empty(&stream->path))
        return false;

    if (dstr_is_empty(&stream->key))
        get_stream_code(&stream->path, &stream->key);

	dstr_depad(&stream->path);
	dstr_depad(&stream->key);

	stream->max_shutdown_time_sec =
		(int)mgw_data_get_int(settings, "max_shutdown_time_sec");

    dstr_copy(&stream->encoder_name, "FMLE/3.0 (compatible; FMSc/1.0)");

	mgw_data_release(settings);
	return true;
}

static int try_connect(struct rtmp_stream *stream)
{
    blog(MGW_LOG_INFO, "Connecting to rtmp url: %s  code: %s ...",
				stream->path.array, stream->key.array);

    if (!RTMP_SetupURL(&stream->rtmp, stream->path.array))
        return MGW_OUTPUT_BAD_PATH;

    RTMP_EnableWrite(&stream->rtmp);

    set_rtmp_dstr(&stream->rtmp.Link.pubUser,   &stream->username);
    set_rtmp_dstr(&stream->rtmp.Link.pubPasswd, &stream->password);
    set_rtmp_dstr(&stream->rtmp.Link.flashVer,  &stream->encoder_name);
    stream->rtmp.Link.swfUrl = stream->rtmp.Link.tcUrl;
    stream->rtmp.en_ip_domain = !dstr_is_empty(&stream->dest_ip);

    if (stream->rtmp.en_ip_domain)
        strncpy(stream->rtmp.ip_domain,
                stream->dest_ip.array, 
                sizeof(stream->rtmp.ip_domain) - 1);

    if (!dstr_is_empty(&stream->netif_name)) {
        if (strncmp(stream->netif_type.array, "ip", 2) == 0) {
            if (netif_str_to_addr(&stream->rtmp.m_bindIP.addr,
                            &stream->rtmp.m_bindIP.addrLen,
                                  stream->netif_name.array)) {
                int len = stream->rtmp.m_bindIP.addrLen;
                bool ipv6 = len == sizeof(struct sockaddr_in6);
                stream->rtmp.set_netopt = NETIF_IP;
                blog(MGW_LOG_INFO, "Binding to IPv%d", ipv6 ? 6 : 4);
            }
        } else if (strncmp(stream->netif_type.array, "net_card", 8) == 0) {
            stream->rtmp.clustering_mtu = stream->netif_mtu;
            stream->rtmp.set_netopt = NETIF_NETCARD;
            strncpy(stream->rtmp.netcard_name, stream->netif_name.array, 
                        sizeof(stream->rtmp.netcard_name) - 1);
        }
    }

    RTMP_AddStream(&stream->rtmp, stream->key.array);

    stream->rtmp.m_outChunkSize        = 4096;
    stream->rtmp.m_bSendChunkSizeInfo  = true;
    stream->rtmp.m_bUseNagle           = false;

    if (!RTMP_Connect(&stream->rtmp, NULL)) {
        stream->output->last_error_status = stream->rtmp.last_error_code;
        return MGW_OUTPUT_CONNECT_FAILED;
    }

    if (!RTMP_ConnectStream(&stream->rtmp, 0))
        return MGW_OUTPUT_INVALID_STREAM;
    blog(MGW_LOG_INFO, "Connecting rtmp stream success!");
    return init_send(stream);
}

static void *connect_thread(void *data)
{
    struct rtmp_stream *stream = data;
    int ret;

    os_set_thread_name("rtmp-stream: connect thread");

    if (!init_connect(stream)) {
        stream->output->signal_stop(stream->output, MGW_OUTPUT_BAD_PATH);
		pthread_detach(stream->connect_thread);
        return NULL;
    }

    if ((ret = try_connect(stream)) != MGW_OUTPUT_SUCCESS) {
        stream->output->signal_stop(stream->output, ret);
        blog(MGW_LOG_INFO, "Connect to %s failed: %d", stream->path.array, ret);
    }

    if (!stopping(stream))
		pthread_detach(stream->connect_thread);

	os_atomic_set_bool(&stream->connecting, false);
	return NULL;
}

static bool rtmp_stream_start(void *data)
{
    struct rtmp_stream *stream = data;
    if (!stream->output->source_is_ready(stream->output))
        return false;

    os_atomic_set_bool(&stream->connecting, true);
	return pthread_create(&stream->connect_thread, NULL, connect_thread, stream) == 0;
}

static void rtmp_stream_stop(void *data)
{
    struct rtmp_stream *stream = data;
    if (stream_valid(data))
		return;

	if (stopping(stream))
		return;
	
	if (connecting(stream))
		pthread_join(stream->connect_thread, NULL);

	stream->stop_time = (uint64_t)os_gettime_ns();

	if (active(stream)) {
		os_event_signal(stream->stop_event);
        pthread_join(stream->send_thread, NULL);
		if (stream->stop_time == 0)
			os_sem_post(stream->send_sem);
	} else {
		stream->output->signal_stop(stream->output, MGW_OUTPUT_SUCCESS);
	}
}

static uint64_t rtmp_stream_get_total_bytes(void *data)
{
    struct rtmp_stream *stream = data;
    if (!stream_valid(data))
        return 0;

    return stream->total_bytes_sent;
}

static void rtmp_stream_apply_update(void *data, mgw_data_t *settings)
{
    struct rtmp_stream *stream = data;
    if (!stream_valid(stream) || !settings)
        return;
}

struct mgw_output_info rtmp_output_info = {
    .id                 = "rtmp_output",
    .flags              = MGW_OUTPUT_AV |
                          MGW_OUTPUT_ENCODED,
    .get_name           = rtmp_stream_get_name,
    .create             = rtmp_stream_create,
    .destroy            = rtmp_stream_destroy,
    .start              = rtmp_stream_start,
    .stop               = rtmp_stream_stop,
    .get_total_bytes    = rtmp_stream_get_total_bytes,
    .get_default        = rtmp_stream_get_default,
    .get_settings       = rtmp_stream_get_settings,
    .update             = rtmp_stream_apply_update
};