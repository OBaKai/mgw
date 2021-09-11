#ifndef __MESSAGE_MESSAGE_DEF_H__
#define __MESSAGE_MESSAGE_DEF_H__

#include "util/c99defs.h"

#ifdef __cpluplus
extern "C" {
#endif

#define MAGIC_CODE	0x66886688
#define MESSAGE_VER	1

/**< 
 * message format consist of three part: [message common head][sub message head][body]
 */
typedef struct msg_attr {
    uint32_t msg_type:8;
    uint32_t priority:4;
    uint32_t mask_flag:1;
    uint32_t ack_req:1;
    uint32_t ack_flag:1;
    uint32_t rsv1:1;
    uint32_t split_msg:1;
    uint32_t last_msg:1;
    uint32_t curr_slice:3;
    uint32_t total_slice:3;
    uint32_t rsv2:8;
}msg_attr_s;

typedef union {
    uint32_t	uint32_val;
    msg_attr_s	bit_val;
}msg_attr_u;

typedef struct common_head {
    uint32_t	magic;
    uint8_t 	msg_ver;
    uint8_t		msg_headlen;
    uint16_t	text_len; 
    uint32_t	msg_no;
    msg_attr_u	msg_attr;
    uint32_t	mask_code;
    uint32_t	ack_no;
}common_head_t;

/**< 32 Byte */
#define COMMON_HEAD_SIZE	sizeof(common_head_t)

typedef enum submsg_type {
	SUBMSG_TYPE_NOP		= 0x0,
	SUBMSG_TYPE_PING	= 0x1,
	SUBMSG_TYPE_PONG	= 0x2,
	SUBMSG_TYPE_AUTHEN	= 0x3,
	SUBMSG_TYPE_TEXT	= 0x4,
}submsg_type_t;

/**************************************************************/
/**< NOP message */
typedef struct nop_head {
	uint8_t		local_addr[13];
	uint8_t		peer_addr[13];
}nop_head_t;

/**************************************************************/
/**< ping message */
typedef struct msg_ping_body {
	uint8_t		rsv[2];
	uint16_t	port;
	uint32_t	ip;
}ping_body_t;

/**************************************************************/
/**< pong message */
typedef struct msg_pong_body {
	uint8_t		authen;	//0:non-authen, 1:have authen
	uint8_t		rsv;
	uint16_t	port;	//local port, bigend
	uint32_t	ip;		//local ip
}pong_body_t;

/**************************************************************/
/**< authentication message */
typedef enum authen_type {
	AUTHEN_TYPE_REQ				= 0x1,	/**< Request Authentication */
	AUTHEN_TYPE_RES				= 0x2,	/**< Respose Authentication */
	AUTHEN_TYPE_PEER_NOT_ONLINE	= 0x3,	/**< Notification that the peer is not online */
	AUTHEN_TYPE_UNAUTHOR		= 0x4,	/**< Client unauthorized notification */
	AUTHEN_TYPE_OFFLINE_REQ		= 0x11, /**< Client offline request */
}authen_t;

typedef enum authen_result {
	AUTHEN_SUCCESS		 	= 0,
	AUTHEN_TOKEN_NOT_FOUND	= 1,
	AUTHEN_TOKEN_TIMEOUT	= 2,
	AUTHEN_TOKEN_NOT_LOGIN	= 3,
	AUTHEN_SYSTEM_ERROR		= 4
}authen_result_t;

/**< access token is a hash string length 40,
 * we need to transform it to hex Byte that two char is a Byte and total length is 20.
 * e.g: 
 * access token string: 6f879fbaa9e20d19a7732717db6c88a70efb3b3c
 * access token Byte: 0x6f, 0x87, 0x9f, ...
 * */
typedef struct authen_req {
	uint8_t		authen_type;		//Defined by authen_type
	uint8_t		rsv[3];
	uint8_t		access_token[20];
}authen_req_t;

typedef struct authen_res {
	uint8_t		authen_type;		//Defined by authen_type
	uint8_t		result;				//defined by authen_result_t
	uint8_t		rsv[2];
	uint8_t		address[13];
}authen_res_t;

/**< Notification that the peer is not online */
typedef struct authen_pno_t {
	uint8_t		authen_type;		//Defined by authen_type
	uint8_t		rsv[3];
	uint8_t		address[13];
}authen_pno_t;

typedef struct {
	uint8_t		authen_type;		//Defined by authen_type
	uint8_t		rsv[3];
}authen_unauthor_t, authen_off_req_t;

/**************************************************************/
/**< text message */
typedef struct text_head {
	uint8_t		local_addr[13];
	uint8_t		rvs1[3];
	uint8_t		peer_addr[13];
	uint8_t		rvs2[3];
}text_head_t;

/**< Text receive body */
typedef struct text_recv {
	uint8_t		text_ver;
	uint8_t		rsv[1];
	char		*body;
}text_recv_t;

/**< Text response body */
typedef struct text_resp {
	uint8_t		rsv[2];
	int32_t		status;
}text_resp_t;

typedef struct text_common_body {
	common_head_t	com_head;
	text_head_t		text_head;
	uint8_t			text_type;
	uint8_t			cmd_type;
}text_com_body_t;

typedef struct text_resp_body {
	text_com_body_t		com_body;
	text_resp_t			resp_body;
}text_resp_body_t;

typedef struct text_recv_body {
	text_com_body_t		com_body;
	text_recv_t			recv_body;
}text_recv_body_t;


typedef enum command_type {
	CMD_START_STREAM		= 0,
	CMD_STOP_STREAM			= 1,
	CMD_REQ_SRCADDR		= 2,
}cmd_t;

#ifdef __cpluplus
}
#endif
#endif  //__MESSAGE_MESSAGE_DEF_H__