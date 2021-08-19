/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Haivision Open SRT (Secure Reliable Transport) protocol
 */
#include "mgw-libsrt.h"
#include <srt/srt.h>

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"

#include "util/base.h"
#include "util/tlog.h"
#include "util/bmem.h"
#include "util/dstr.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/* This is for MPEG-TS and it's a default SRTO_PAYLOADSIZE for SRTT_LIVE (8 TS packets) */
#ifndef SRT_LIVE_DEFAULT_PAYLOAD_SIZE
#define SRT_LIVE_DEFAULT_PAYLOAD_SIZE 1316
#endif

/* This is the maximum payload size for Live mode, should you have a different payload type than MPEG-TS */
#ifndef SRT_LIVE_MAX_PAYLOAD_SIZE
#define SRT_LIVE_MAX_PAYLOAD_SIZE 1456
#endif

#define AVIO_FLAG_NONBLOCK 8
#define POLLING_TIME 100 /// Time in milliseconds between interrupt check

typedef struct srt_context {
    int fd;
    int eid;
    int64_t rw_timeout;
    int64_t listen_timeout;
    int recv_buffer_size;
    int send_buffer_size;

    int64_t maxbw;
    int pbkeylen;
    char *passphrase;
#if SRT_VERSION_VALUE >= 0x010302
    int enforced_encryption;
    int kmrefreshrate;
    int kmpreannounce;
#endif
    int mss;
    int ffs;
    int ipttl;
    int iptos;
    int64_t inputbw;
    int oheadbw;
    int64_t latency;
    int tlpktdrop;
    int nakreport;
    int64_t connect_timeout;
    int payload_size;
    int64_t rcvlatency;
    int64_t peerlatency;
    enum srt_mode mode;
    int sndbuf;
    int rcvbuf;
    int lossmaxttl;
    int minversion;
    char *streamid;
    char *smoother;
    int messageapi;
    SRT_TRANSTYPE transtype;
    int linger;
	int tsbpd;

	int flags;
	int max_packet_size;
	volatile int is_streamed;
	srt_int_cb *int_cb;
	struct dstr filename;
} srt_context;

#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
#define OFFSET(x) offsetof(srt_context, x)
static const AVOption libsrt_options[] = {
    { "timeout",        "Timeout of socket I/O operations (in microseconds)",                   OFFSET(rw_timeout),       AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, .flags = D|E },
    { "listen_timeout", "Connection awaiting timeout (in microseconds)" ,                       OFFSET(listen_timeout),   AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, .flags = D|E },
    { "send_buffer_size", "Socket send buffer size (in bytes)",                                 OFFSET(send_buffer_size), AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
    { "recv_buffer_size", "Socket receive buffer size (in bytes)",                              OFFSET(recv_buffer_size), AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
    { "pkt_size",       "Maximum SRT packet size",                                              OFFSET(payload_size),     AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, SRT_LIVE_MAX_PAYLOAD_SIZE, .flags = D|E, "payload_size" },
    { "payload_size",   "Maximum SRT packet size",                                              OFFSET(payload_size),     AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, SRT_LIVE_MAX_PAYLOAD_SIZE, .flags = D|E, "payload_size" },
    { "ts_size",        NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = SRT_LIVE_DEFAULT_PAYLOAD_SIZE }, INT_MIN, INT_MAX, .flags = D|E, "payload_size" },
    { "max_size",       NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = SRT_LIVE_MAX_PAYLOAD_SIZE },     INT_MIN, INT_MAX, .flags = D|E, "payload_size" },
    { "maxbw",          "Maximum bandwidth (bytes per second) that the connection can use",     OFFSET(maxbw),            AV_OPT_TYPE_INT64,    { .i64 = -1 }, -1, INT64_MAX, .flags = D|E },
    { "pbkeylen",       "Crypto key len in bytes {16,24,32} Default: 16 (128-bit)",             OFFSET(pbkeylen),         AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, 32,        .flags = D|E },
    { "passphrase",     "Crypto PBKDF2 Passphrase size[0,10..64] 0:disable crypto",             OFFSET(passphrase),       AV_OPT_TYPE_STRING,   { .str = NULL },              .flags = D|E },
#if SRT_VERSION_VALUE >= 0x010302
    { "enforced_encryption", "Enforces that both connection parties have the same passphrase set",                              OFFSET(enforced_encryption), AV_OPT_TYPE_BOOL,  { .i64 = -1 }, -1, 1,         .flags = D|E },
    { "kmrefreshrate",       "The number of packets to be transmitted after which the encryption key is switched to a new key", OFFSET(kmrefreshrate),       AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
    { "kmpreannounce",       "The interval between when a new encryption key is sent and when switchover occurs",               OFFSET(kmpreannounce),       AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
#endif
    { "mss",            "The Maximum Segment Size",                                             OFFSET(mss),              AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, 1500,      .flags = D|E },
    { "ffs",            "Flight flag size (window size) (in bytes)",                            OFFSET(ffs),              AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
    { "ipttl",          "IP Time To Live",                                                      OFFSET(ipttl),            AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, 255,       .flags = D|E },
    { "iptos",          "IP Type of Service",                                                   OFFSET(iptos),            AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, 255,       .flags = D|E },
    { "inputbw",        "Estimated input stream rate",                                          OFFSET(inputbw),          AV_OPT_TYPE_INT64,    { .i64 = -1 }, -1, INT64_MAX, .flags = D|E },
    { "oheadbw",        "MaxBW ceiling based on % over input stream rate",                      OFFSET(oheadbw),          AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, 100,       .flags = D|E },
    { "latency",        "receiver delay (in microseconds) to absorb bursts of missed packet retransmissions",                     OFFSET(latency),          AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, .flags = D|E },
    { "tsbpddelay",     "deprecated, same effect as latency option",                            OFFSET(latency),          AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, .flags = D|E },
    { "rcvlatency",     "receive latency (in microseconds)",                                    OFFSET(rcvlatency),       AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, .flags = D|E },
    { "peerlatency",    "peer latency (in microseconds)",                                       OFFSET(peerlatency),      AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, .flags = D|E },
    { "tlpktdrop",      "Enable too-late pkt drop",                                             OFFSET(tlpktdrop),        AV_OPT_TYPE_BOOL,     { .i64 = -1 }, -1, 1,         .flags = D|E },
    { "nakreport",      "Enable receiver to send periodic NAK reports",                         OFFSET(nakreport),        AV_OPT_TYPE_BOOL,     { .i64 = -1 }, -1, 1,         .flags = D|E },
    { "connect_timeout", "Connect timeout(in milliseconds). Caller default: 3000, rendezvous (x 10)",                            OFFSET(connect_timeout),  AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, INT64_MAX, .flags = D|E },
    { "mode",           "Connection mode (caller, listener, rendezvous)",                       OFFSET(mode),             AV_OPT_TYPE_INT,      { .i64 = SRT_MODE_CALLER }, SRT_MODE_CALLER, SRT_MODE_RENDEZVOUS, .flags = D|E, "mode" },
    { "caller",         NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = SRT_MODE_CALLER },     INT_MIN, INT_MAX, .flags = D|E, "mode" },
    { "listener",       NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = SRT_MODE_LISTENER },   INT_MIN, INT_MAX, .flags = D|E, "mode" },
    { "rendezvous",     NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = SRT_MODE_RENDEZVOUS }, INT_MIN, INT_MAX, .flags = D|E, "mode" },
    { "sndbuf",         "Send buffer size (in bytes)",                                          OFFSET(sndbuf),           AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
    { "rcvbuf",         "Receive buffer size (in bytes)",                                       OFFSET(rcvbuf),           AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
    { "lossmaxttl",     "Maximum possible packet reorder tolerance",                            OFFSET(lossmaxttl),       AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
    { "minversion",     "The minimum SRT version that is required from the peer",               OFFSET(minversion),       AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
    { "streamid",       "A string of up to 512 characters that an Initiator can pass to a Responder",  OFFSET(streamid),  AV_OPT_TYPE_STRING,   { .str = NULL },              .flags = D|E },
    { "srt_streamid",   "A string of up to 512 characters that an Initiator can pass to a Responder",  OFFSET(streamid),  AV_OPT_TYPE_STRING,   { .str = NULL },              .flags = D|E },
    { "smoother",       "The type of Smoother used for the transmission for that socket",       OFFSET(smoother),         AV_OPT_TYPE_STRING,   { .str = NULL },              .flags = D|E },
    { "messageapi",     "Enable message API",                                                   OFFSET(messageapi),       AV_OPT_TYPE_BOOL,     { .i64 = -1 }, -1, 1,         .flags = D|E },
    { "transtype",      "The transmission type for the socket",                                 OFFSET(transtype),        AV_OPT_TYPE_INT,      { .i64 = SRTT_INVALID }, SRTT_LIVE, SRTT_INVALID, .flags = D|E, "transtype" },
    { "live",           NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = SRTT_LIVE }, INT_MIN, INT_MAX, .flags = D|E, "transtype" },
    { "file",           NULL, 0, AV_OPT_TYPE_CONST,  { .i64 = SRTT_FILE }, INT_MIN, INT_MAX, .flags = D|E, "transtype" },
    { "linger",         "Number of seconds that the socket waits for unsent data when closing", OFFSET(linger),           AV_OPT_TYPE_INT,      { .i64 = -1 }, -1, INT_MAX,   .flags = D|E },
    { "tsbpd",          "Timestamp-based packet delivery",                                      OFFSET(tsbpd),            AV_OPT_TYPE_BOOL,     { .i64 = -1 }, -1, 1,         .flags = D|E },
    { NULL }
};

static int libsrt_neterrno(void)
{
    int os_errno;
    int err = srt_getlasterror(&os_errno);
    if (err == SRT_EASYNCRCV || err == SRT_EASYNCSND)
        return AVERROR(EAGAIN);
    tlog(TLOG_ERROR, "%s\n", srt_getlasterror_str());
    return os_errno ? AVERROR(os_errno) : AVERROR_UNKNOWN;
}

static int libsrt_socket_nonblock(int socket, int enable)
{
    int ret, blocking = enable ? 0 : 1;
    /* Setting SRTO_{SND,RCV}SYN options to 1 enable blocking mode, setting them to 0 enable non-blocking mode. */
    ret = srt_setsockopt(socket, 0, SRTO_SNDSYN, &blocking, sizeof(blocking));
    if (ret < 0)
        return ret;
    return srt_setsockopt(socket, 0, SRTO_RCVSYN, &blocking, sizeof(blocking));
}

static int libsrt_epoll_create(int fd, int write)
{
    int modes = SRT_EPOLL_ERR | (write ? SRT_EPOLL_OUT : SRT_EPOLL_IN);
    int eid = srt_epoll_create();
    if (eid < 0)
        return libsrt_neterrno();
    if (srt_epoll_add_usock(eid, fd, &modes) < 0) {
        srt_epoll_release(eid);
        return libsrt_neterrno();
    }
    return eid;
}

static int libsrt_network_wait_fd(int eid, int write)
{
    int ret, len = 1, errlen = 1;
    SRTSOCKET ready[1];
    SRTSOCKET error[1];

    if (write) {
        ret = srt_epoll_wait(eid, error, &errlen, ready, &len, POLLING_TIME, 0, 0, 0, 0);
    } else {
        ret = srt_epoll_wait(eid, ready, &len, error, &errlen, POLLING_TIME, 0, 0, 0, 0);
    }
    if (ret < 0) {
        if (srt_getlasterror(NULL) == SRT_ETIMEOUT)
            ret = AVERROR(EAGAIN);
        else
            ret = libsrt_neterrno();
    } else {
        ret = errlen ? AVERROR(EIO) : 0;
    }
    return ret;
}

/* TODO de-duplicate code from ff_network_wait_fd_timeout() */
static inline int check_stream_interrupt(srt_int_cb *cb)
{
	if (cb && cb->callback)
		return cb->callback(cb->opaque);
	return 0;
}

static int libsrt_network_wait_fd_timeout(int eid, int write, int64_t timeout, srt_int_cb *int_cb)
{
    int ret;
    int64_t wait_start = 0;

    while (1) {
        if (check_stream_interrupt(int_cb))
            return AVERROR_EXIT;
        ret = libsrt_network_wait_fd(eid, write);
        if (ret != AVERROR(EAGAIN))
            return ret;
        if (timeout > 0) {
            if (!wait_start)
                wait_start = av_gettime_relative();
            else if (av_gettime_relative() - wait_start > timeout)
                return AVERROR(ETIMEDOUT);
        }
    }
}

static int libsrt_listen(void *priv_data, int eid, int fd, const struct sockaddr *addr, socklen_t addrlen, int64_t timeout)
{
	srt_context *s = priv_data;
    int ret;
    int reuse = 1;
    if (srt_setsockopt(fd, SOL_SOCKET, SRTO_REUSEADDR, &reuse, sizeof(reuse))) {
        tlog(TLOG_WARN, "setsockopt(SRTO_REUSEADDR) failed\n");
    }
    ret = srt_bind(fd, addr, addrlen);
    if (ret)
        return libsrt_neterrno();

    ret = srt_listen(fd, 1);
    if (ret)
        return libsrt_neterrno();

    ret = libsrt_network_wait_fd_timeout(eid, 1, timeout, s->int_cb);
    if (ret < 0)
        return ret;

    ret = srt_accept(fd, NULL, NULL);
    if (ret < 0)
        return libsrt_neterrno();
    if (libsrt_socket_nonblock(ret, 1) < 0)
        tlog(TLOG_DEBUG, "libsrt_socket_nonblock failed\n");

    return ret;
}

static int libsrt_listen_connect(void *priv_data, int eid, int fd,
								const struct sockaddr *addr,socklen_t addrlen,
								int64_t timeout, int will_try_next)
{
    int ret;
	srt_context *s = priv_data;
    ret = srt_connect(fd, addr, addrlen);
    if (ret < 0)
        return libsrt_neterrno();

    ret = libsrt_network_wait_fd_timeout(eid, 1, timeout, s->int_cb);
    if (ret < 0) {
        if (will_try_next) {
            tlog(TLOG_WARN,
                   "Connection to %s failed (%s), trying next address\n",
                   dstr_is_empty(&s->filename)?"":s->filename.array, av_err2str(ret));
        } else {
            tlog(TLOG_ERROR, "Connection to %s failed: %s\n",
                   dstr_is_empty(&s->filename)?"":s->filename.array, av_err2str(ret));
        }
    }
    return ret;
}

static int libsrt_setsockopt(int fd, SRT_SOCKOPT optname, const char * optnamestr, const void * optval, int optlen)
{
    if (srt_setsockopt(fd, 0, optname, optval, optlen) < 0) {
        tlog(TLOG_ERROR, "failed to set option %s on socket: %s\n", optnamestr, srt_getlasterror_str());
        return AVERROR(EIO);
    }
    return 0;
}

static int libsrt_getsockopt(int fd, SRT_SOCKOPT optname, const char * optnamestr, void * optval, int * optlen)
{
    if (srt_getsockopt(fd, 0, optname, optval, optlen) < 0) {
        tlog(TLOG_ERROR, "failed to get option %s on socket: %s\n", optnamestr, srt_getlasterror_str());
        return AVERROR(EIO);
    }
    return 0;
}

/* - The "POST" options can be altered any time on a connected socket.
     They MAY have also some meaning when set prior to connecting; such
     option is SRTO_RCVSYN, which makes connect/accept call asynchronous.
     Because of that this option is treated special way in this app. */
static int libsrt_set_options_post(void *priv_data, int fd)
{
    srt_context *s = priv_data;

    if ((s->inputbw >= 0 && libsrt_setsockopt(fd, SRTO_INPUTBW, "SRTO_INPUTBW", &s->inputbw, sizeof(s->inputbw)) < 0) ||
        (s->oheadbw >= 0 && libsrt_setsockopt(fd, SRTO_OHEADBW, "SRTO_OHEADBW", &s->oheadbw, sizeof(s->oheadbw)) < 0)) {
        return AVERROR(EIO);
    }
    return 0;
}

/* - The "PRE" options must be set prior to connecting and can't be altered
     on a connected socket, however if set on a listening socket, they are
     derived by accept-ed socket. */
static int libsrt_set_options_pre(void *priv_data, int fd)
{
    srt_context *s = priv_data;
    int yes = 1;
    int latency = s->latency / 1000;
    int rcvlatency = s->rcvlatency / 1000;
    int peerlatency = s->peerlatency / 1000;
    int connect_timeout = s->connect_timeout;

    if ((s->mode == SRT_MODE_RENDEZVOUS && libsrt_setsockopt(fd, SRTO_RENDEZVOUS, "SRTO_RENDEZVOUS", &yes, sizeof(yes)) < 0) ||
        (s->transtype != SRTT_INVALID && libsrt_setsockopt(fd, SRTO_TRANSTYPE, "SRTO_TRANSTYPE", &s->transtype, sizeof(s->transtype)) < 0) ||
        (s->maxbw >= 0 && libsrt_setsockopt(fd, SRTO_MAXBW, "SRTO_MAXBW", &s->maxbw, sizeof(s->maxbw)) < 0) ||
        (s->pbkeylen >= 0 && libsrt_setsockopt(fd, SRTO_PBKEYLEN, "SRTO_PBKEYLEN", &s->pbkeylen, sizeof(s->pbkeylen)) < 0) ||
        (s->passphrase && libsrt_setsockopt(fd, SRTO_PASSPHRASE, "SRTO_PASSPHRASE", s->passphrase, strlen(s->passphrase)) < 0) ||
#if SRT_VERSION_VALUE >= 0x010302
#if SRT_VERSION_VALUE >= 0x010401
        (s->enforced_encryption >= 0 && libsrt_setsockopt(fd, SRTO_ENFORCEDENCRYPTION, "SRTO_ENFORCEDENCRYPTION", &s->enforced_encryption, sizeof(s->enforced_encryption)) < 0) ||
#else
        /* SRTO_STRICTENC == SRTO_ENFORCEDENCRYPTION (53), but for compatibility, we used SRTO_STRICTENC */
        (s->enforced_encryption >= 0 && libsrt_setsockopt(h, fd, SRTO_STRICTENC, "SRTO_STRICTENC", &s->enforced_encryption, sizeof(s->enforced_encryption)) < 0) ||
#endif
        (s->kmrefreshrate >= 0 && libsrt_setsockopt(fd, SRTO_KMREFRESHRATE, "SRTO_KMREFRESHRATE", &s->kmrefreshrate, sizeof(s->kmrefreshrate)) < 0) ||
        (s->kmpreannounce >= 0 && libsrt_setsockopt(fd, SRTO_KMPREANNOUNCE, "SRTO_KMPREANNOUNCE", &s->kmpreannounce, sizeof(s->kmpreannounce)) < 0) ||
#endif
        (s->mss >= 0 && libsrt_setsockopt(fd, SRTO_MSS, "SRTO_MSS", &s->mss, sizeof(s->mss)) < 0) ||
        (s->ffs >= 0 && libsrt_setsockopt(fd, SRTO_FC, "SRTO_FC", &s->ffs, sizeof(s->ffs)) < 0) ||
        (s->ipttl >= 0 && libsrt_setsockopt(fd, SRTO_IPTTL, "SRTO_IPTTL", &s->ipttl, sizeof(s->ipttl)) < 0) ||
        (s->iptos >= 0 && libsrt_setsockopt(fd, SRTO_IPTOS, "SRTO_IPTOS", &s->iptos, sizeof(s->iptos)) < 0) ||
        (s->latency >= 0 && libsrt_setsockopt(fd, SRTO_LATENCY, "SRTO_LATENCY", &latency, sizeof(latency)) < 0) ||
        (s->rcvlatency >= 0 && libsrt_setsockopt(fd, SRTO_RCVLATENCY, "SRTO_RCVLATENCY", &rcvlatency, sizeof(rcvlatency)) < 0) ||
        (s->peerlatency >= 0 && libsrt_setsockopt(fd, SRTO_PEERLATENCY, "SRTO_PEERLATENCY", &peerlatency, sizeof(peerlatency)) < 0) ||
        (s->tlpktdrop >= 0 && libsrt_setsockopt(fd, SRTO_TLPKTDROP, "SRTO_TLPKDROP", &s->tlpktdrop, sizeof(s->tlpktdrop)) < 0) ||
        (s->nakreport >= 0 && libsrt_setsockopt(fd, SRTO_NAKREPORT, "SRTO_NAKREPORT", &s->nakreport, sizeof(s->nakreport)) < 0) ||
        (connect_timeout >= 0 && libsrt_setsockopt(fd, SRTO_CONNTIMEO, "SRTO_CONNTIMEO", &connect_timeout, sizeof(connect_timeout)) <0 ) ||
        (s->sndbuf >= 0 && libsrt_setsockopt(fd, SRTO_SNDBUF, "SRTO_SNDBUF", &s->sndbuf, sizeof(s->sndbuf)) < 0) ||
        (s->rcvbuf >= 0 && libsrt_setsockopt(fd, SRTO_RCVBUF, "SRTO_RCVBUF", &s->rcvbuf, sizeof(s->rcvbuf)) < 0) ||
        (s->lossmaxttl >= 0 && libsrt_setsockopt(fd, SRTO_LOSSMAXTTL, "SRTO_LOSSMAXTTL", &s->lossmaxttl, sizeof(s->lossmaxttl)) < 0) ||
        (s->minversion >= 0 && libsrt_setsockopt(fd, SRTO_MINVERSION, "SRTO_MINVERSION", &s->minversion, sizeof(s->minversion)) < 0) ||
        (s->streamid && libsrt_setsockopt(fd, SRTO_STREAMID, "SRTO_STREAMID", s->streamid, strlen(s->streamid)) < 0) ||
#if SRT_VERSION_VALUE >= 0x010401
        (s->smoother && libsrt_setsockopt(fd, SRTO_CONGESTION, "SRTO_CONGESTION", s->smoother, strlen(s->smoother)) < 0) ||
#else
        (s->smoother && libsrt_setsockopt(fd, SRTO_SMOOTHER, "SRTO_SMOOTHER", s->smoother, strlen(s->smoother)) < 0) ||
#endif
        (s->messageapi >= 0 && libsrt_setsockopt(fd, SRTO_MESSAGEAPI, "SRTO_MESSAGEAPI", &s->messageapi, sizeof(s->messageapi)) < 0) ||
        (s->payload_size >= 0 && libsrt_setsockopt(fd, SRTO_PAYLOADSIZE, "SRTO_PAYLOADSIZE", &s->payload_size, sizeof(s->payload_size)) < 0) ||
        ((s->flags & SRT_IO_FLAG_WRITE) && libsrt_setsockopt(fd, SRTO_SENDER, "SRTO_SENDER", &yes, sizeof(yes)) < 0) ||
        (s->tsbpd >= 0 && libsrt_setsockopt(fd, SRTO_TSBPDMODE, "SRTO_TSBPDMODE", &s->tsbpd, sizeof(s->tsbpd)) < 0)) {
        return AVERROR(EIO);
    }

    if (s->linger >= 0) {
        struct linger lin;
        lin.l_linger = s->linger;
        lin.l_onoff  = lin.l_linger > 0 ? 1 : 0;
        if (libsrt_setsockopt(fd, SRTO_LINGER, "SRTO_LINGER", &lin, sizeof(lin)) < 0)
            return AVERROR(EIO);
    }
    return 0;
}


static int libsrt_setup(void *priv_data, const char *uri, int flags)
{
    struct addrinfo hints = { 0 }, *ai, *cur_ai;
    int port, fd = -1;
    srt_context *s = priv_data;
    const char *p;
    char buf[256];
    int ret;
    char hostname[1024],proto[1024],path[1024];
    char portstr[10];
    int64_t open_timeout = 0;
    int eid, write_eid;

    av_url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname),
        &port, path, sizeof(path), uri);
    if (strcmp(proto, "srt"))
        return AVERROR(EINVAL);
    if (port <= 0 || port >= 65536) {
        tlog(TLOG_ERROR, "Port missing in uri\n");
        return AVERROR(EINVAL);
    }
    p = strchr(uri, '?');
    if (p) {
        if (av_find_info_tag(buf, sizeof(buf), "timeout", p)) {
            s->rw_timeout = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "listen_timeout", p)) {
            s->listen_timeout = strtol(buf, NULL, 10);
        }
    }
    if (s->rw_timeout >= 0) {
        open_timeout = s->rw_timeout;
    }
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (s->mode == SRT_MODE_LISTENER)
        hints.ai_flags |= AI_PASSIVE;
    ret = getaddrinfo(hostname[0] ? hostname : NULL, portstr, &hints, &ai);
    if (ret) {
        tlog(TLOG_ERROR, "Failed to resolve hostname %s: %s\n",
               hostname, gai_strerror(ret));
        return AVERROR(EIO);
    }

    cur_ai = ai;

 restart:

    fd = srt_socket(cur_ai->ai_family, cur_ai->ai_socktype, 0);
    if (fd < 0) {
        ret = libsrt_neterrno();
        goto fail;
    }

    if ((ret = libsrt_set_options_pre(priv_data, fd)) < 0) {
        goto fail;
    }

    /* Set the socket's send or receive buffer sizes, if specified.
       If unspecified or setting fails, system default is used. */
    if (s->recv_buffer_size > 0) {
        srt_setsockopt(fd, SOL_SOCKET, SRTO_UDP_RCVBUF, &s->recv_buffer_size, sizeof (s->recv_buffer_size));
    }
    if (s->send_buffer_size > 0) {
        srt_setsockopt(fd, SOL_SOCKET, SRTO_UDP_SNDBUF, &s->send_buffer_size, sizeof (s->send_buffer_size));
    }
    if (libsrt_socket_nonblock(fd, 1) < 0)
        tlog(TLOG_DEBUG, "libsrt_socket_nonblock failed\n");

    ret = write_eid = libsrt_epoll_create(fd, 1);
    if (ret < 0)
        goto fail1;
    if (s->mode == SRT_MODE_LISTENER) {
        // multi-client
        ret = libsrt_listen(priv_data, write_eid, fd, cur_ai->ai_addr, cur_ai->ai_addrlen, s->listen_timeout);
        srt_epoll_release(write_eid);
        if (ret < 0)
            goto fail1;
        srt_close(fd);
        fd = ret;
    } else {
        if (s->mode == SRT_MODE_RENDEZVOUS) {
            if (srt_bind(fd, cur_ai->ai_addr, cur_ai->ai_addrlen)) {
                ret = libsrt_neterrno();
                srt_epoll_release(write_eid);
                goto fail1;
            }
        }

        ret = libsrt_listen_connect(priv_data, write_eid, fd, cur_ai->ai_addr, cur_ai->ai_addrlen,
                                    open_timeout, !!cur_ai->ai_next);
        srt_epoll_release(write_eid);
        if (ret < 0) {
            if (ret == AVERROR_EXIT)
                goto fail1;
            else
                goto fail;
        }
    }
    if ((ret = libsrt_set_options_post(priv_data, fd)) < 0) {
        goto fail;
    }

    if (flags & SRT_IO_FLAG_WRITE) {
        int packet_size = 0;
        int optlen = sizeof(packet_size);
        ret = libsrt_getsockopt(fd, SRTO_PAYLOADSIZE, "SRTO_PAYLOADSIZE", &packet_size, &optlen);
        if (ret < 0)
            goto fail1;
        if (packet_size > 0)
            s->max_packet_size = packet_size;
    }

    ret = eid = libsrt_epoll_create(fd, flags & SRT_IO_FLAG_WRITE);
    if (eid < 0)
        goto fail1;

    s->is_streamed = 1;
    s->fd = fd;
    s->eid = eid;

    freeaddrinfo(ai);
    return 0;

 fail:
    if (cur_ai->ai_next) {
        /* Retry with the next sockaddr */
        cur_ai = cur_ai->ai_next;
        if (fd >= 0)
            srt_close(fd);
        ret = 0;
        goto restart;
    }
 fail1:
    if (fd >= 0)
        srt_close(fd);
    freeaddrinfo(ai);
    return ret;
}

int mgw_libsrt_open(void *priv_data, const char *uri, int flags)
{
    srt_context *s = priv_data;
    const char * p;
    char buf[256];
    int ret = 0;

    if (srt_startup() < 0) {
        return AVERROR_UNKNOWN;
    }

    /* SRT options (srt/srt.h) */
    p = strchr(uri, '?');
    if (p) {
        if (av_find_info_tag(buf, sizeof(buf), "maxbw", p)) {
            s->maxbw = strtoll(buf, NULL, 0);
        }
        if (av_find_info_tag(buf, sizeof(buf), "pbkeylen", p)) {
            s->pbkeylen = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "passphrase", p)) {
            av_freep(&s->passphrase);
            s->passphrase = av_strndup(buf, strlen(buf));
        }
#if SRT_VERSION_VALUE >= 0x010302
        if (av_find_info_tag(buf, sizeof(buf), "enforced_encryption", p)) {
            s->enforced_encryption = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "kmrefreshrate", p)) {
            s->kmrefreshrate = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "kmpreannounce", p)) {
            s->kmpreannounce = strtol(buf, NULL, 10);
        }
#endif
        if (av_find_info_tag(buf, sizeof(buf), "mss", p)) {
            s->mss = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "ffs", p)) {
            s->ffs = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "ipttl", p)) {
            s->ipttl = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "iptos", p)) {
            s->iptos = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "inputbw", p)) {
            s->inputbw = strtoll(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "oheadbw", p)) {
            s->oheadbw = strtoll(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "latency", p)) {
            s->latency = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "tsbpddelay", p)) {
            s->latency = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "rcvlatency", p)) {
            s->rcvlatency = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "peerlatency", p)) {
            s->peerlatency = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "tlpktdrop", p)) {
            s->tlpktdrop = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "nakreport", p)) {
            s->nakreport = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "connect_timeout", p)) {
            s->connect_timeout = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "payload_size", p) ||
            av_find_info_tag(buf, sizeof(buf), "pkt_size", p)) {
            s->payload_size = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "mode", p)) {
            if (!strcmp(buf, "caller")) {
                s->mode = SRT_MODE_CALLER;
            } else if (!strcmp(buf, "listener")) {
                s->mode = SRT_MODE_LISTENER;
            } else if (!strcmp(buf, "rendezvous")) {
                s->mode = SRT_MODE_RENDEZVOUS;
            } else {
                ret = AVERROR(EINVAL);
                goto err;
            }
        }
        if (av_find_info_tag(buf, sizeof(buf), "sndbuf", p)) {
            s->sndbuf = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "rcvbuf", p)) {
            s->rcvbuf = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "lossmaxttl", p)) {
            s->lossmaxttl = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "minversion", p)) {
            s->minversion = strtol(buf, NULL, 0);
        }
        if (av_find_info_tag(buf, sizeof(buf), "streamid", p)) {
            av_freep(&s->streamid);
            s->streamid = av_strdup(buf);
            if (!s->streamid) {
                ret = AVERROR(ENOMEM);
                goto err;
            }
        }
        if (av_find_info_tag(buf, sizeof(buf), "smoother", p)) {
            av_freep(&s->smoother);
            s->smoother = av_strdup(buf);
            if(!s->smoother) {
                ret = AVERROR(ENOMEM);
                goto err;
            }
        }
        if (av_find_info_tag(buf, sizeof(buf), "messageapi", p)) {
            s->messageapi = strtol(buf, NULL, 10);
        }
        if (av_find_info_tag(buf, sizeof(buf), "transtype", p)) {
            if (!strcmp(buf, "live")) {
                s->transtype = SRTT_LIVE;
            } else if (!strcmp(buf, "file")) {
                s->transtype = SRTT_FILE;
            } else {
                ret = AVERROR(EINVAL);
                goto err;
            }
        }
        if (av_find_info_tag(buf, sizeof(buf), "linger", p)) {
            s->linger = strtol(buf, NULL, 10);
        }
    }
    ret = libsrt_setup(priv_data, uri, flags);
    if (ret < 0)
        goto err;
    return 0;

err:
    av_freep(&s->smoother);
    av_freep(&s->streamid);
    srt_cleanup();
    return ret;
}

int mgw_libsrt_read(void *priv_data, uint8_t *buf, int size)
{
    srt_context *s = priv_data;
    int ret;

    if (!(s->flags & AVIO_FLAG_NONBLOCK)) {
        ret = libsrt_network_wait_fd_timeout(s->eid, 0, s->rw_timeout, s->int_cb);
        if (ret)
            return ret;
    }

    ret = srt_recvmsg(s->fd, (char *)buf, size);
    if (ret < 0) {
        ret = libsrt_neterrno();
    }

    return ret;
}

int mgw_libsrt_write(void *priv_data, const uint8_t *buf, int size)
{
    srt_context *s = priv_data;
    int ret;

    if (!(s->flags & AVIO_FLAG_NONBLOCK)) {
        ret = libsrt_network_wait_fd_timeout(s->eid, 1, s->rw_timeout, s->int_cb);
        if (ret) {
			blog(MGW_LOG_ERROR, "Waiting fd timeout failed, ret:%s", av_err2str(ret));
			return ret;
		}
    }

    ret = srt_sendmsg(s->fd, (char *)buf, size, -1, 0);
    if (ret < 0) {
        ret = libsrt_neterrno();
		blog(MGW_LOG_ERROR, "Send data failed, ret:%d", ret);
    }

    return ret;
}

int mgw_libsrt_close(void *priv_data)
{
    srt_context *s = priv_data;

    srt_epoll_release(s->eid);
    srt_close(s->fd);

    srt_cleanup();

    return 0;
}

int mgw_libsrt_get_file_handle(void *priv_data)
{
    srt_context *s = priv_data;
    return s->fd;
}

void *mgw_libsrt_create(srt_int_cb *int_cb, const char *filename, enum srt_mode mode)
{
	if (!int_cb) {
		tlog(TLOG_WARN, "libsrt wouldn't create because int_cb is NULL!\n");
		return NULL;
	}
	blog(MGW_LOG_INFO, "-------------->> get libsrt configurations!");
	srt_context *s = bzalloc(sizeof(srt_context));
	s->int_cb = int_cb;

	/**< Set srt context default value */
	s->rw_timeout = -1;
	s->listen_timeout = -1;
	s->recv_buffer_size = -1;
	s->send_buffer_size = -1;
	s->maxbw = -1;
	s->pbkeylen = -1;
	s->enforced_encryption = -1;
	s->kmrefreshrate = -1;
	s->kmpreannounce = -1;
	s->mss = -1;
	s->ffs = -1;
	s->ipttl = -1;
	s->iptos = -1;
	s->inputbw = -1;
	s->oheadbw = -1;
	s->latency = 200000;
	s->tlpktdrop = -1;
	s->nakreport = -1;
	s->connect_timeout = -1;
	s->payload_size = 1128;
	s->rcvlatency = -1;
	s->peerlatency = -1;
	s->mode = 0;//1;
	s->sndbuf = -1;
	s->rcvbuf = -1;
	s->lossmaxttl = -1;
	s->minversion = -1;
	s->messageapi = -1;
	s->transtype = SRTT_LIVE;
	s->linger = -1;
	// s->tsbpd = 1;

	dstr_copy(&s->filename, filename);

	return s;
}

void mgw_libsrt_destroy(void *priv_data)
{
	srt_context *s = priv_data;
	if (!s) return;

	dstr_free(&s->filename);
	bfree(s->int_cb);
	bfree(s);
}


/**< Settings and Get */
int mgw_libsrt_get_payload_size(void *priv_data)
{
	return priv_data ? ((struct srt_context*)priv_data)->payload_size : 0;
}

void mgw_libsrt_set_payload_size(void *priv_data, int size)
{
	struct srt_context *s = priv_data;
	if (priv_data && size > 188)
		if (0 == libsrt_setsockopt(s->fd, SRTO_PAYLOADSIZE, \
					"SRTO_PAYLOADSIZE", &size, sizeof(size)))
			s->payload_size = size;
}

int64_t mgw_libsrt_get_maxbw(void *priv_data)
{
	return priv_data ? ((struct srt_context*)priv_data)->maxbw : 0;
}

void mgw_libsrt_set_maxbw(void *priv_data, int64_t size)
{
	struct srt_context *s = priv_data;
	if (priv_data && size > 20)
		if (0 == libsrt_setsockopt(s->fd, SRTO_MAXBW, \
					"SRTO_MAXBW", &size, sizeof(size)))
			s->maxbw = size;
}