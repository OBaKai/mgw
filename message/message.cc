#include "message.h"
#include "google/protobuf/util/json_util.h"

extern "C" {
#include "mgw.h"
#include "util/bmem.h"
#include "util/tlog.h"
#include <openssl/md5.h>
#include <openssl/sha.h>
}

#define ACCESS "access"
#define CAMERAS "cameras"
#define BUFFER_SIZE	1024

struct user_infos {
	/**< From configuration file */
	std::string api_key;
	std::string secret_key;
	std::string server_host;
	std::string sn;
	std::string type;
	std::string vendor;

	/**< System generate */
	std::string version;

	/**< From server */
	std::string serial_num;
	std::string challenge;
	std::string access_token;
	std::string token;
	std::string ws_uri;

	int expires;
	int cid_flag;
	int hb_interval;
	uint16_t port;
};

Message::Message()
	:settings_(NULL),
	curl_(curl_easy_init()),
	infos_(new user_infos)
{
	signal(SIGPIPE, SIG_IGN);
	curl_global_init(CURL_GLOBAL_ALL);

	curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 15L);
	curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1);
}

Message::~Message()
{
	mgw_data_release(settings_);
	curl_easy_cleanup(curl_);
	if (infos_) {
		delete infos_;
		infos_ = nullptr;
	}
	if (ws_client_)
		wsclient_destroy((void*)ws_client_);
}

msg_status Message::Register(const std::string &configPath)
{

	if (configPath.empty())
		return msg_status::MSG_STATUS_PARMER_INVALID;

	if (!(settings_ = mgw_data_create_from_json_file(configPath.data()))) {
		std::cout << "Configuration apth:" << configPath << " invalid, please check!" << std::endl;
		return msg_status::MSG_STATUS_BAD_PATH;
	}

	/**< Get configuration from file */
#ifdef DEBUG
	const char *api_key = "test_api_key", *secret_key = "test_secret_key", *host = "test_host";
#else
	const char *api_key = "release_api_key", *secret_key = "release_secret_key", *host = "release_host";
#endif
	infos_->api_key = mgw_data_get_string(settings_, api_key);
	infos_->secret_key = mgw_data_get_string(settings_, secret_key);
	infos_->server_host = mgw_data_get_string(settings_, host);
	infos_->vendor = mgw_data_get_string(settings_, "vendor");
	infos_->sn = mgw_data_get_string(settings_, "sn");
	infos_->type = mgw_data_get_string(settings_, "type");

	/**< Get version internal */
	infos_->version = mgw_get_version_string();

	/**< Start to register proccessing... */
	/**< 1.AccessRequest  2.AuthenRequest  3.RegisterInternal */
	if (!AccessRequest()) {
		tlog(TLOG_ERROR, "Tried to call AccessRequest failed...");
		return msg_status::MSG_STATUS_ACCESSREQ_FAILED;
	}

	if (!AuthenRequest()) {
		tlog(TLOG_ERROR, "Tried to call AuthenRequest failed...");
		return msg_status::MSG_STATUS_AUTHENREQ_FAILED;
	}

	if (!RegisterInternal()) {
		tlog(TLOG_INFO, "Tried to call RegisterInternal failed...");
		return msg_status::MSG_STATUS_REGISTERREQ_FAILED;
	}

	/**< Create and start ws client */
	if (!InitMessageEnv()) {
		tlog(TLOG_INFO, "Tried to create and start ws client failed...");
		return msg_status::MSG_STATUS_WSCLIENT_START_FAILED;
	}

	return msg_status::MSG_STATUS_SUCCESS;
}

void Message::UnRegister(void)
{
}

static int curl_req_result(const void *ptr, size_t s, size_t n, void *p)
{
	std::string *str = static_cast<std::string *>(p);
	str->append((const char*)ptr, s*n);
	return str->size();
}

static std::string SendAPIReqMessage(CURL *curl, const std::string &host,
				const std::string &uri, const std::string &body)
{
	CURLcode ret = CURLE_OK;
	std::string result;
	struct curl_slist *slist1 = NULL;

	tlog(TLOG_INFO, "Request body:%s", body.data());

	curl_easy_reset(curl);

	curl_easy_setopt(curl, CURLOPT_URL, uri.data());
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result);
	if (!body.empty())
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());

	if (!host.empty()) {
		std::string req_head("Host: ");
		req_head.append(host);
		slist1 = curl_slist_append(slist1, req_head.data());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist1);
	}

	// curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, 
	// 			[](const void *ptr, size_t s, size_t n, void *p) {
	// 				tlog(TLOG_INFO, "result data: %s", ptr);
	// 				std::string *str = static_cast<std::string *>(p);
	// 				str->append((const char*)ptr, s*n);
	// 				return str->size();
	// 		});
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_req_result);

	ret = curl_easy_perform(curl);
	if (CURLE_OK != ret) {
		tlog(TLOG_ERROR, "request uri:%s, body:%s failed, ret:%s\n",
					uri.data(), body.data(), curl_easy_strerror(ret));
	}

	if (slist1) {
		curl_slist_free_all(slist1);
		slist1 = NULL;
	}

	tlog(TLOG_INFO, "Result string: %s", result.data());

	return std::move(result);
}

static inline std::string get_vhost(const std::string &server_host)
{
	const char *end = NULL;
	const char *p = server_host.data();
	p = strstr(p, "://");
	if (p)
		p += 3;
	end = p;
	while ((*end) != '0' && (*end) != '/')
		end++;
	if ((*end) == '/')
		return std::string(p, end-p);
	else
		return "";
}

bool Message::AccessRequest(void)
{
	char buffer[BUFFER_SIZE] = {};
	std::string body;
	std::string uri;

	snprintf(buffer, BUFFER_SIZE, "sn=%s&type=%s&sub_type=mgw&key=%s&version=%s&vendor=%s",
				infos_->sn.data(), infos_->type.data(), infos_->api_key.data(),
				infos_->version.data(), infos_->vendor.data());
	body.append(buffer, strlen(buffer) + 1);
	std::cout << "access request body: " << body << std::endl;

	memset(buffer, 0, BUFFER_SIZE);
	snprintf(buffer, BUFFER_SIZE, "%s/access/access_req", infos_->server_host.data());
	uri.append(buffer, strlen(buffer) + 1);
	std::cout << "request uri: " << uri << std::endl;

	std::string ret_str = SendAPIReqMessage(curl_, get_vhost(infos_->server_host), uri, body);
	if (ret_str.empty()) {
		tlog(TLOG_ERROR, "Request access request have a error...");
		return false;
	}

	mgw_data_t *data = mgw_data_create_from_json(ret_str.data());
	const char *serial_num = mgw_data_get_string(data, "serial");
	const char *challenge = mgw_data_get_string(data, "challenge");
	infos_->serial_num.clear();
	infos_->challenge.clear();
	infos_->serial_num.append(serial_num);
	infos_->challenge.append(challenge);

	mgw_data_release(data);
	return ret_str.size() > 0;
}

bool Message::AuthenRequest(void)
{
	char buffer[BUFFER_SIZE] = {};
	char verify[128] = {0};
    unsigned char result[128] = {};
	int i  = 0, verify_size = 0, sha1_result_len = 20, md5_result_len = 16;
	std::string uri;
	std::string body;

	MD5((const unsigned char *)infos_->secret_key.data(), infos_->secret_key.size(), result);

	for ( i = 0 ; i < md5_result_len; i++ )
		verify_size += sprintf((verify+verify_size), "%02x", result[i]);

	memset(result, 0, md5_result_len);
	MD5((const unsigned char *)infos_->challenge.data(), infos_->challenge.size(), result);
	for ( i = 0 ; i < md5_result_len; i++ )
		verify_size += sprintf((verify+verify_size), "%02x", result[i]);

	memset(result, 0, md5_result_len);
	MD5((const unsigned char *)infos_->api_key.data(), infos_->api_key.size(), result);
	for ( i = 0 ; i < md5_result_len; i++ )
		verify_size += sprintf((verify+verify_size), "%02x", result[i]);

	memset(result, 0, 128);
	SHA1((const unsigned char *)verify, strlen(verify), result);

	memset(verify, 0, 128);
	verify_size = 0;
	for ( i = 0 ; i < sha1_result_len; i++ )
		verify_size += sprintf((verify+verify_size), "%02x", result[i]);

	snprintf(buffer, BUFFER_SIZE, "serial=%s&key=%s&verify=%s",
				infos_->serial_num.data(), infos_->api_key.data(), verify);
	body.append(buffer);

	memset(buffer, 0, BUFFER_SIZE);
	snprintf(buffer, BUFFER_SIZE, "%s/access/challenge", infos_->server_host.data());
	uri.append(buffer);

	std::string ret_str = SendAPIReqMessage(curl_, get_vhost(infos_->server_host), uri, body);
	if (ret_str.empty()) {
		tlog(TLOG_ERROR, "Request authen have a error...");
		return false;
	}

	mgw_data_t *data = mgw_data_create_from_json(ret_str.data());
	infos_->expires = mgw_data_get_int(data, "expires");
	const char *access_token = mgw_data_get_string(data, "access_token");
	infos_->access_token.clear();
	infos_->access_token.append(access_token);

	mgw_data_release(data);
	return ret_str.size() > 0;
}

bool Message::RegisterInternal(void)
{
	char buffer[BUFFER_SIZE] = {};
	std::string uri;
	std::string body;

	snprintf(buffer, BUFFER_SIZE, "key=%s&access_token=%s&server_sn=%s&vendor=%s",
									infos_->api_key.data(), infos_->access_token.data(),
									infos_->sn.data(), infos_->vendor.data());
	body.append(buffer);

	memset(buffer, 0, BUFFER_SIZE);
	snprintf(buffer, BUFFER_SIZE, "%s/mgw_server/register", infos_->server_host.data());
	uri.append(buffer);

	std::string ret_str = SendAPIReqMessage(curl_, std::string(), uri, body);
	if (ret_str.empty()) {
		tlog(TLOG_ERROR, "Request register have a error...");
		return false;
	}
	mgw_data_t *data = mgw_data_create_from_json(ret_str.data());
	mgw_data_t *msgcenter = mgw_data_get_obj(data, "msgcenter");

	/**< TODO: get register result from ret_str */
	infos_->server_host = mgw_data_get_string(msgcenter, "host");
	infos_->port = mgw_data_get_int(msgcenter, "port");
	infos_->hb_interval = mgw_data_get_int(msgcenter, "ping_interval");
	infos_->ws_uri = mgw_data_get_string(msgcenter, "ws");
	tlog(TLOG_INFO, "host:%s, port:%d, interval:%d",
			infos_->server_host.data(), infos_->port, infos_->hb_interval);

	mgw_data_release(data);
	mgw_data_release(msgcenter);
	return true;
}

static mgw_data_t *get_info_from_pb(const char *data, cmd_t cmd)
{
	// std::string reqStr;
	// google::protobuf::util::MessageToJsonString(msg, &reqStr);

	// if (!reqStr.empty())
	// 	return mgw_data_create_from_json(reqStr.data());
	// else
	// 	return NULL;
	return NULL;
}

static msg_resp_t wsclient_callback(void *opaque, char *data, cmd_t cmd)
{
	msg_resp_t resp = {};
	struct ws_client *client = (struct ws_client *)opaque;
	if (!client || !data) {
		resp.status = STATUS_PARAMER_INVALID;
		return resp;
	}

	if (!get_info_from_pb((const char *)data, cmd))
		resp.status = STATUS_PARSE_ERROR;

	return resp;
}

bool Message::InitMessageEnv(void)
{
	struct wsclient_info *ws_info = (struct wsclient_info*)bzalloc(sizeof(struct wsclient_info));
	ws_info->access_token = bstrdup(infos_->access_token.data());
	ws_info->cb = wsclient_callback;
	ws_info->hb_interval = infos_->hb_interval;
	ws_info->opaque = (void*)this;
	ws_info->uri = bstrdup(infos_->ws_uri.data());
	ws_info->port = infos_->port;

	ws_client_ = wsclient_create(ws_info);
	if (!ws_client_) {
		bfree((void *)ws_info->access_token);
		bfree((void *)ws_info->uri);
		bfree((void *)ws_info);
		return false;
	}

	if (0 != wsclient_start(ws_client_))
		return false;

	return true;
}

// static char *serial_message_to_pb(mgw_data_t *data, int cmd)
// {
// 	google::protobuf::StringPiece snd_str;
// 	google::protobuf::util::JsonStringToMessage(snd_str, Message *message);
// }

int Message::SendMessage(mgw_data_t *data, int cmd)
{

	// if (STATUS_SUCCESS != wsclient_send(ws_client_, const char *buf, size_t size)) {

	// }
	return 0;
}