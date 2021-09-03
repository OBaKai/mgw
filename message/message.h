#ifndef _MGW_MESSAGE_MESSAGE_H_
#define _MGW_MESSAGE_MESSAGE_H_

extern "C" {
#include "util/mgw-data.h"
#include "curl/curl.h"
#include "ws-client.h"
}

#include <string>

/**< Use this macro to transform a name to string,note: name never be a macro! */
#define TOSTR(name) #name

struct user_infos;

enum class msg_status:int {
	MSG_STATUS_SUCCESS					= 0,
	MSG_STATUS_PARMER_INVALID			= -1,
	MSG_STATUS_BAD_PATH					= -2,
	MSG_STATUS_ACCESSREQ_FAILED			= -3,
	MSG_STATUS_AUTHENREQ_FAILED			= -4,
	MSG_STATUS_REGISTERREQ_FAILED		= -5,
	MSG_STATUS_WSCLIENT_START_FAILED	= -6,
	MSG_STATUS_EXISTED					= -7
};

class Message
{
public:
	static Message &GetInstance(void) {
		static Message msg;
		return msg;
	}

	~Message();
	msg_status Register(const std::string &configPath);
	msg_status Register(mgw_data_t *settings);
	void UnRegister(void);
	bool Alive(void) const {return wsclient_actived(ws_client_);}
	int SendMessage(mgw_data_t *data, int cmd);
private:
	Message();
	Message(const Message &) = delete;
	Message(const Message &&) = delete;
	Message operator=(const Message &) = delete;

	/********************** API Register **********************************/
	/**
	 * body include those field: key, type, sub_type, vendor, version, sn
	 * retrun : error, serial, challenge
	*/
	bool AccessRequest(void);
	/**
	 * body include:serial, key, verify
	 * return: error, expires, access_token
	*/
	bool AuthenRequest(void);
	/**
	 * APP body: key, access_token, loginname, nickname, password, sms_code, referrer
	 * Device body: key, access_token, sn, cid_flag
	 * return: error, userid,
	 * 		   user{id, loginname, phone, nickename, pic, regtime, province, city,
	 * 				district, sign, sex, birth, additions[], ref, points, invited_user_counts,
	 * 				hash_id, cid, token}, msgcenter_ip, msgcenter_port, ping_interval, config_str
	*/
	bool RegisterInternal(void);
	bool InitMessageEnv(void);

	static void *ReceivingThread(const Message &msg);

    /* data */
	bool				registered_;
	CURL				*curl_;
	void				*ws_client_;
	mgw_data_t			*settings_;
	struct user_infos	*infos_;
};

#endif  //_MGW_MESSAGE_MESSAGE_H_