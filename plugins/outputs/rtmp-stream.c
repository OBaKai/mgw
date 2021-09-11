#include <signal.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "mgw-internal.h"
#include "mgw-outputs.h"

#include "util/base.h"
#include "util/tlog.h"
#include "util/dstr.h"
#include "util/platform.h"
#include "util/callback-handle.h"

#include "formats/flv-mux.h"
#include "librtmp/log.h"
#include "librtmp/rtmp.h"

#undef  RTMP_MODULE_NAME
#define RTMP_MODULE_NAME     "rtmp_stream"

#define NETIF_DEF       0
#define NETIF_NETCARD   1
#define NETIF_IP        2
#define NETIF_MTU_DEF   1350
#define NETIF_TYPE_DEF  "default"
#define NETIF_NAME_DEF  ""

//#define TEST_STREAM_TIMESTAMP	1

static pthread_once_t rtmp_context_once = PTHREAD_ONCE_INIT;

struct rtmp_stream {
    mgw_output_t    *output;

    bool            new_socket_loop;
    bool            sent_headers;
    uint64_t        start_time, rct_time, stop_time;
    uint64_t        total_bytes_sent;
    uint64_t        audio_drop_frames, video_drop_frames;
    uint64_t        start_dts_offset;
    uint64_t        sent_frames;
	uint32_t		connect_count;
	int64_t			last_dts;

    volatile bool   connecting;
    pthread_t       connect_thread;

    volatile bool    active;
	volatile bool    disconnected;
	pthread_t        send_thread;

    os_sem_t         *send_sem;
	os_event_t       *stop_event;

    uint32_t        netif_mtu;
    struct dstr     netif_type, netif_name;

    /* used by rtmp stream internal */
    uint16_t        port;
    struct dstr     path, key, dest_ip;
    struct dstr     username, password;
    struct dstr     encoder_name;
	uint8_t			*frame_buffer;

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
    return !!data && !!stream->output;
}

static inline void log_rtmp(int level, const char *fmt, va_list args)
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

static mgw_data_t *rtmp_stream_get_default(void)
{
	mgw_data_t *def_settings = mgw_data_create();
    mgw_data_set_int(def_settings, "netif_mtu", NETIF_MTU_DEF);
    mgw_data_set_string(def_settings, "netif_type", NETIF_TYPE_DEF);
    mgw_data_set_string(def_settings, "netif_name", NETIF_NAME_DEF);

	return def_settings;
}

static mgw_data_t *rtmp_stream_get_settings(void *data)
{
    struct rtmp_stream *stream = data;
    if (!stream_valid(data))
        return NULL;

	mgw_data_t *settings = mgw_data_create();

    mgw_data_set_int(settings, "drop_frames", 
            stream->audio_drop_frames + stream->video_drop_frames);
	mgw_data_set_int(settings, "total_bytes_sent", stream->total_bytes_sent);
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

    return settings;
}

static const char *rtmp_stream_get_name(void *data)
{
    UNUSED_PARAMETER(data);
    return RTMP_MODULE_NAME;
}

static inline int do_source_proc_handler(struct rtmp_stream *stream,
				const char *name, call_params_t *params) {
	proc_handler_t *handler = stream->output->get_source_proc_handler(stream->output);
	return proc_handler_do(handler, name, params);
}

static inline int do_output_proc_handler(struct rtmp_stream *stream,
				const char *name, call_params_t *params) {
	proc_handler_t *handler = stream->output->context.procs;
	return proc_handler_do(handler, name, params);
}

static void rtmp_stream_destroy(void *data)
{
    struct rtmp_stream *stream = data;
	//RTMP *r = (RTMP*)&stream->rtmp;
    //int idx = 0;
    /* stop connecting */
    tlog(TLOG_INFO, "stop and release rtmp stream:%s/%s\n", stream->path.array, stream->key.array);
    if (connecting(stream) || active(stream)) {
        if (stream->connecting)
            pthread_join(stream->connect_thread, NULL);

        stream->stop_time = (uint64_t)time(NULL);
        os_event_signal(stream->stop_event);

        if(active(stream)) {
            os_sem_post(stream->send_sem);
			os_atomic_set_bool(&stream->active, false);
            pthread_join(stream->send_thread, NULL);
        }
    }

    dstr_free(&stream->path);
    dstr_free(&stream->key);
    dstr_free(&stream->dest_ip);
    dstr_free(&stream->username);
    dstr_free(&stream->password);
    dstr_free(&stream->netif_name);
	dstr_free(&stream->encoder_name);

    os_event_destroy(stream->stop_event);
	os_sem_destroy(stream->send_sem);
	bfree(stream->frame_buffer);
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

	stream->frame_buffer = bzalloc(MGW_MAX_PACKET_SIZE + MGW_AVCC_HEADER_SIZE);
	//os_event_signal(stream->stop_event);

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

static bool send_packet_internal(struct rtmp_stream *stream,
		struct encoder_packet *packet, bool is_header, size_t idx)
{
	uint8_t *data;
	size_t  size;
	int     recv_size = 0;
	bool    ret = false;

	uint64_t tick = packet->pts / 1000;

	if (!stream->start_dts_offset && !is_header) {
		if (ENCODER_VIDEO == packet->type)
			stream->start_dts_offset = tick;
		else
			return true;
	}

	if (!stream->new_socket_loop) {
		ret = ioctl(stream->rtmp.m_sb.sb_socket, FIONREAD, &recv_size);
		if (ret >= 0 && recv_size > 0) {
			if (!discard_recv_data(stream, (size_t)recv_size)) {
                ret = false;
                goto error;
            }
		}
	}

	packet->dts = packet->pts = tick - stream->start_dts_offset;
	if (packet->dts < stream->last_dts) {
		tlog(TLOG_ERROR, "current dst:%"PRId64" is small than last:%"PRId64"\n", packet->dts, stream->last_dts);
	}

	flv_packet_mux(packet, is_header ? 0 : stream->start_dts_offset,
			&data, &size, is_header);

	ret = (RTMP_Write(&stream->rtmp, (char*)data, (int)size, (int)idx) > 0);
	bfree(data);
	
	stream->last_dts = packet->dts;
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

	call_params_t params = {};
	if (0 != do_source_proc_handler(stream, "get_encoder_settings", &params)) {
		tlog(TLOG_ERROR, "Couldn't get encoder settings!\n");
		return false;
	}

	bool success = flv_meta_data((mgw_data_t*)params.out,
						&meta_data, &meta_data_size, false, idx);
	if (success) {
		success = RTMP_Write(&stream->rtmp, (char*)meta_data,
				(int)meta_data_size, (int)idx) >= 0;
		mgw_data_release((mgw_data_t*)params.out);
		bfree(meta_data);
	}

	return success;
}

static bool send_audio_header(struct rtmp_stream *stream, size_t idx,
		bool *next, int64_t ts)
{
	mgw_output_t  *context  = stream->output;
	call_params_t params = {};
	char header[8] = {0xaf, 0x00};
	struct encoder_packet packet   = {
		.type         = ENCODER_AUDIO,
		.timebase_den = 1,
		.pts		  = ts,
		.dts		  = ts
	};

	if (0 != do_source_proc_handler(stream, "get_encoder_settings", &params)) {
		tlog(TLOG_ERROR, "Couldn't get source settings!\n");
		return false;
	}
	int channels = mgw_data_get_int((mgw_data_t*)params.out, "channels");
	int samplesize = mgw_data_get_int((mgw_data_t*)params.out, "samplesize");
	if (samplesize == 8) header[0] &= 0xfd;
	if (channels == 1) header[0] &= 0xfe;
	mgw_data_release((mgw_data_t*)params.out);

	if (0 != do_source_proc_handler(stream, "get_audio_header", &params)) {
		tlog(TLOG_ERROR, "Couldn't get audio header!\n");
		return false;
	}
	//must be flv header + AudioSpecificConfig -- aac
	packet.size = params.out_size + 2;
	memcpy(header + 2, params.out, params.out_size);
	packet.data = bmemdup(header, packet.size);
	bfree(params.out);
	return send_packet_internal(stream, &packet, true, idx);
}

static bool send_video_header(struct rtmp_stream *stream, int64_t ts)
{
	mgw_output_t  *context  = stream->output;
	call_params_t params = {};
	struct encoder_packet packet   = {
		.type         = ENCODER_VIDEO,
		.timebase_den = 1,
		.keyframe     = true,
		.pts		  = ts,
		.dts		  = ts
	};

	if (0 != do_source_proc_handler(stream, "get_video_header", &params)) {
		tlog(TLOG_ERROR, "Couldn't get audio header!\n");
		return false;
	}
	// must be AVCDecoderConfigurationRecord -- avc
	packet.size = params.out_size;
	packet.data = params.out;
	return send_packet_internal(stream, &packet, true, 0);
}

static inline bool send_headers(struct rtmp_stream *stream, int64_t ts)
{
	stream->sent_headers = true;
	size_t i = 0;
	bool next = true;

	if (!send_audio_header(stream, i++, &next, ts))
		return false;
	if (!send_video_header(stream, ts))
		return false;

	return true;
}

/**< Bigend */
static inline uint8_t *put_be32(uint8_t **output, uint32_t nVal)
{
    (*output)[3] = nVal & 0xff;
    (*output)[2] = nVal >> 8;
    (*output)[1] = nVal >> 16;
    (*output)[0] = nVal >> 24;
    return (*output)+4;
}

static bool send_key_frame(struct rtmp_stream *stream, struct encoder_packet *packet)
{
	uint8_t *data = NULL;
	size_t size = 0;

	size = mgw_avc_get_keyframe((const uint8_t*)packet->data,
										packet->size, &data);
	if (size <= 0) {
		blog(MGW_LOG_ERROR, "Couldn't find key frame!, size = %ld", size);
		return true;
	}

	packet->data = data - 4;
	put_be32(&packet->data, size);
	packet->size = size + 4;
	return send_packet_internal(stream, packet, false, packet->track_idx);
}

/**< Modify avcc format to annexB format */
static inline bool send_packet(struct rtmp_stream *stream, struct encoder_packet *packet)
{
	if (packet->type == ENCODER_AUDIO) {
		if (!(packet->data[1] & 0x01)) {
			int packet_len = ((packet->data[3]&0x03) << 11) + \
							(packet->data[4] << 3) + ((packet->data[5] & 0xe0) >> 5);
			if (packet->size == packet_len) {
				packet->data += 9;
				packet->size -= 9;
			}
		} else {
			packet->data += 7;
			packet->size -= 7;
		}
	} else if (packet->type == ENCODER_VIDEO) {
		int start_code = mgw_avc_get_startcode_len(packet->data);
		if (start_code == 3) {
			packet->data -= 1;
			put_be32(&packet->data, packet->size - 3);
			packet->size += 1;
		} else if (start_code == 4) {
			put_be32(&packet->data, packet->size - 4);
		} else if (start_code < 0) {
			blog(MGW_LOG_ERROR, "Couldn't find the NALU start code, "
						"data[0]:%02x, data[1]:%02x, data[2]:%02x, data[3]:%02x, data[3]:%02x",
						packet->data[0], packet->data[1], packet->data[2],packet->data[3],packet->data[4]);
			return true;
		}
	}

	return send_packet_internal(stream, packet, false, packet->track_idx);
}

static void *send_thread(void *data)
{
	struct rtmp_stream *stream = data;
	int ret = 0, sleep_freq = 6;
	bool success = false;

#define SET_DISCONNECT(stream) do { \
		tlog(TLOG_ERROR, "error occur! will disconnect !\n"); \
		os_atomic_set_bool(&stream->disconnected, true); \
		goto error; \
	} while (false)

	os_set_thread_name("rtmp-stream: send_thread");
	blog(MGW_LOG_INFO, "rtmp-stream thread running!");
    while(active(stream)) {
		if (stopping(stream))
			break;

		struct encoder_packet packet = {};
		packet.data = stream->frame_buffer + MGW_AVCC_HEADER_SIZE;
		if (0 >= (ret = stream->output->get_encoder_packet(
								stream->output, &packet))) {
			usleep(1 * 1000);
            continue;
        }

		if (!stream->sent_headers ||
			(FRAME_PRIORITY_LOW == packet.priority &&
			 packet.keyframe && ENCODER_VIDEO == packet.type)) {

			if (!send_headers(stream, stream->sent_headers?packet.pts:0))
				SET_DISCONNECT(stream);
		}

		if (packet.keyframe && ENCODER_VIDEO == packet.type) {
			if (!send_key_frame(stream, &packet))
				SET_DISCONNECT(stream);
		} else {
			if (!send_packet(stream, &packet))
				SET_DISCONNECT(stream);
		}

		stream->sent_frames++;
		if (0 == (stream->sent_frames % sleep_freq))
			usleep(10 *1000);
	}
error:
#undef SET_DISCONNECT

	if (disconnected(stream)) 
		tlog(TLOG_INFO, "Disconnected from %s/%s\n", stream->path.array, stream->key.array);
	else
		tlog(TLOG_INFO, "User stopped the stream\n");

	stream->output->last_error_status = stream->rtmp.last_error_code;
	RTMP_Close(&stream->rtmp);
	if (!stopping(stream) && disconnected(stream)) {
		pthread_detach(stream->send_thread);
		tlog(TLOG_INFO, "rtmp stream send_thread signal stop, ret:%d", MGW_DISCONNECTED);
		// stream->output->signal_stop(stream->output, MGW_OUTPUT_DISCONNECTED);
		ret = MGW_DISCONNECTED;
		call_params_t params = {.in = &ret};
		do_output_proc_handler(stream, "signal_stop", &params);
	}

	stream->sent_headers = false;
	os_event_reset(stream->stop_event);
	success = os_atomic_set_bool(&stream->active, false);

	UNUSED_PARAMETER(success);
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
        tlog(TLOG_ERROR, "Disconnected while attempting to connect to server!\n");
        stream->output->last_error_status = stream->rtmp.last_error_code;
        return MGW_DISCONNECTED;
    }

    reset_semaphore(stream);
    os_atomic_set_bool(&stream->active, true);
    if (pthread_create(&stream->send_thread, NULL, send_thread, stream) != 0) {
        RTMP_Close(&stream->rtmp);
        blog(MGW_LOG_ERROR, "Failed to create send thread!");
        os_atomic_set_bool(&stream->active, false);
        return MGW_ERROR;
    }

    return MGW_SUCCESS;
}

static bool init_connect(struct rtmp_stream *stream)
{
	if (stopping(stream)) {
		pthread_join(stream->send_thread, NULL);
	}
    os_atomic_set_bool(&stream->disconnected, false);

    mgw_data_t *output_settings = mgw_data_newref(stream->output->context.settings);
    const char *path = mgw_data_get_string(output_settings, "path");
    const char *key = mgw_data_get_string(output_settings, "key");
    const char *username = mgw_data_get_string(output_settings, "username");
	const char *password = mgw_data_get_string(output_settings, "password");
	const char *dest_ip = mgw_data_get_string(output_settings, "dest_ip");
	const char *netif_type = mgw_data_get_string(output_settings, "netif_type");
	const char *netif_name = mgw_data_get_string(output_settings, "netif_name");

	call_params_t params = {};
	if (0 != do_source_proc_handler(stream, "get_encoder_settings", &params))
		tlog(TLOG_ERROR, "Couldn't get encoder settings!\n");

    if (path && key) {
        dstr_copy(&stream->path,        path);
	    dstr_copy(&stream->key,         key);
    }
	if (username && password) {
		dstr_copy(&stream->username, username);
		dstr_copy(&stream->password, password);
	}

	if (dest_ip) {
		dstr_copy(&stream->dest_ip, dest_ip);
	}

	if (netif_name && netif_type) {
		dstr_copy(&stream->netif_type, netif_type);
    	dstr_copy(&stream->netif_name, netif_name);
	}

    if (dstr_is_empty(&stream->path))
        return false;

    if (dstr_is_empty(&stream->key))
        get_stream_code(&stream->path, &stream->key);

	dstr_depad(&stream->path);
	dstr_depad(&stream->key);

	stream->max_shutdown_time_sec =
		(int)mgw_data_get_int(output_settings, "max_shutdown_time_sec");

    dstr_copy(&stream->encoder_name, "FMLE/3.0 (compatible; FMSc/1.0)");

	mgw_data_release(output_settings);
	if (params.out)
		mgw_data_release((mgw_data_t *)params.out);
	return true;
}

static int try_connect(struct rtmp_stream *stream)
{
    tlog(TLOG_INFO, "Connecting to rtmp url: %s  code: %s ...",
				stream->path.array, stream->key.array);

    if (!RTMP_SetupURL(&stream->rtmp, stream->path.array))
        return MGW_BAD_PATH;

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
        return MGW_CONNECT_FAILED;
    }

    if (!RTMP_ConnectStream(&stream->rtmp, 0))
        return MGW_INVALID_STREAM;
    tlog(TLOG_INFO, "Connecting rtmp stream success!\n");
    return init_send(stream);
}

static void *connect_thread(void *data)
{
	int ret;
	call_params_t params = {.in = &ret};
    struct rtmp_stream *stream = data;

    os_set_thread_name("rtmp-stream: connect thread");

    if (!init_connect(stream)) {
		tlog(TLOG_INFO, "rtmp stream init_connect signal stop, ret:%d", MGW_BAD_PATH);
		ret = MGW_BAD_PATH;
		do_output_proc_handler(stream, "signal_stop", &params);
        // stream->output->signal_stop(stream->output, MGW_OUTPUT_BAD_PATH);
		pthread_detach(stream->connect_thread);
        return NULL;
    }

	stream->connect_count++;
    if ((ret = try_connect(stream)) != MGW_SUCCESS) {
		tlog(TLOG_ERROR, "Connect to %s failed: %d\n", stream->path.array, ret);
		do_output_proc_handler(stream, "signal_stop", &params);
        // stream->output->signal_stop(stream->output, ret);
    }

    if (!stopping(stream))
		pthread_detach(stream->connect_thread);

	os_atomic_set_bool(&stream->connecting, false);
	return NULL;
}

static bool rtmp_stream_start(void *data)
{
    struct rtmp_stream *stream = data;
    if (!do_output_proc_handler(stream, "source_ready", NULL)) {
		blog(MGW_LOG_ERROR, "source is not ready!");
		return false;
    }

    os_atomic_set_bool(&stream->connecting, true);
	return pthread_create(&stream->connect_thread, NULL, connect_thread, stream) == 0;
}

static void rtmp_stream_stop(void *data)
{
    struct rtmp_stream *stream = data;
	RTMP *r = &stream->rtmp;
	int ret = 0;
	call_params_t params = {.in = &ret};

    if (!stream_valid(data))
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
		tlog(TLOG_INFO, "rtmp stream stop signal stop, ret:%d\n", MGW_SUCCESS);
		ret = MGW_SUCCESS;
		do_output_proc_handler(stream, "signal_stop", &params);
		// stream->output->signal_stop(stream->output, MGW_OUTPUT_SUCCESS);
	}

	for (int idx = 0; idx < r->Link.nStreams; idx++) {
		bfree(r->Link.streams[idx].playpath.av_val);
	}
	r->Link.curStreamIdx = 0;
	r->Link.nStreams = 0;

	stream->last_dts = 0;
	stream->start_dts_offset = 0;
	stream->start_time = 0;
	stream->rct_time = 0;
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

public_visi struct mgw_output_info rtmp_output_info = {
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