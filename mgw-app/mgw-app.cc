/**< C++ header files */
#include "mgw-app.h"
#include "message.h"
#include "mgw-events.h"

#include <memory>

/**< C header files */
extern "C" {
#include "mgw.h"
#include "util/base.h"
#include "util/tlog.h"
#include "util/mgw-data.h"
#include "util/platform.h"
#include <stdio.h>
#include <unistd.h>
}

#define CONFIG_MESSAGE			1

MGWApp::MGWApp():
	started_(false)
{

}

MGWApp::~MGWApp()
{
	ExitApp();
}

bool MGWApp::Startup(const std::string &configFile)
{
	if (configFile.empty()) return false;
	if (started_) return true;

	mgw_data_t *config = mgw_data_create_from_json_file(configFile.data());
	if (!mgw_startup(config)) {
		tlog(TLOG_ERROR, "Startup mgw internal failed!\n");
		goto error;
	}
	started_ = true;
error:
	mgw_data_release(config);
	return started_;
}

void MGWApp::ExitApp(void)
{
	if (started_)
		mgw_shutdown();
}

bool MGWApp::AttempToReset(void)
{
	return started_ && mgw_reset_all() == 0;
}

std::string &&MGWApp::GetAPIVersion(void)
{
	return std::move(std::string(mgw_get_version_string()));
}

int MGWApp::StartOutputStream(mgw_data_t *settings, mgw_data_t **result)
{
	int ret = MSG_STATUS_SUCCESS;
    /**< A device include a stream by default, search stream by device information */
    mgw_data_t *dev_info = mgw_data_get_obj(settings, "device");
	mgw_data_t *output_info = mgw_data_get_obj(settings, "stream");
    if (!dev_info || !output_info)
        return MSG_STATUS_PARMER_INVALID;

    /**< Do not exist the device, create a new device */
	mgw_device_t *device = NULL;
	const char *dev_sn = mgw_data_get_string(dev_info, "sn");
	const char *dev_type = mgw_data_get_string(dev_info, "type");

    if (!(device = mgw_get_weak_device_by_name(dev_sn)))
		device = mgw_device_create(dev_type, dev_sn, dev_info);

    if (device && !mgw_device_has_stream(device)) {
		int stream_channel = mgw_data_get_int(output_info, "src_channel");
		std::string stream_name = "stream" + std::to_string(stream_channel);

		mgw_data_t *stream_settings = mgw_data_create();
		mgw_data_array_t *outputs = mgw_data_array_create();

		mgw_data_array_push_back(outputs, output_info);

		mgw_data_set_string(stream_settings, "name", stream_name.data());
		mgw_data_set_bool(stream_settings, "is_private", false);
		mgw_data_set_array(stream_settings, "outputs", outputs);

		if (!mgw_device_addstream_with_outputs(device, stream_settings))
			tlog(TLOG_ERROR, "Device tried to add stream[%s] with outputs failed", stream_name.data());

		mgw_data_array_release(outputs);
		mgw_data_release(stream_settings);

		/**< Request a source stream address if there are no source stream */
		Message &msg = Message::GetInstance();
		msg.SendMessage(dev_info, CMD_REQ_SRCADDR);

	} else if (device) {
		/**< Start a output stream directly if there are exist a stream */
		int stream_channel = mgw_data_get_int(output_info, "src_channel");
		std::string stream_name = "stream" + std::to_string(stream_channel);

		ret = mgw_device_add_output_to_stream(device, stream_name.data(), output_info);
	}

	if (dev_info)
		mgw_data_release(dev_info);
	if (output_info)
		mgw_data_release(output_info);

	return ret;
}

void MGWApp::StopOutputStream(mgw_data_t *settings)
{
    /**< Stop a exist output stream  */
	mgw_data_t *dev_info = mgw_data_get_obj(settings, "device");
	mgw_data_t *output_info = mgw_data_get_obj(settings, "stream");

	if (!dev_info || !output_info) return;

	mgw_device_t *device = NULL;
	const char *dev_sn = mgw_data_get_string(dev_info, "sn");
	if ((device = mgw_get_weak_device_by_name(dev_sn)))
		mgw_device_release_output_from_stream(device, NULL, output_info);

	if (dev_info)
		mgw_data_release(dev_info);
	if (output_info)
		mgw_data_release(output_info);
}

int MGWApp::StartSourceStream(mgw_data_t *settings, mgw_data_t **result)
{
	int ret = MSG_STATUS_SUCCESS;
    /**< Start a source stream if there are no stream and the stream already in whitelist */
	mgw_data_t *dev_info = mgw_data_get_obj(settings, "device");
	mgw_data_t *source_info = mgw_data_get_obj(settings, "stream");

	mgw_device_t *device = NULL;
	const char *dev_sn = mgw_data_get_string(dev_info, "sn");
	if (!(device = mgw_get_weak_device_by_name(dev_sn))) {
		tlog(TLOG_ERROR, "Couldn't find device:%s", dev_sn);
		return MSG_STATUS_NODEVICE;
	}

	if (!mgw_device_has_stream(device)) {
		int channel = mgw_data_get_int(source_info, "src_channel");
		std::string stream_name = "stream" + std::to_string(channel);

		mgw_data_t *stream_info = mgw_data_create();
		mgw_data_set_string(stream_info, "name", stream_name.data());
		mgw_data_set_bool(stream_info, "is_private", false);
		mgw_data_set_obj(stream_info, "source", source_info);

		ret = mgw_device_addstream_with_source(device, stream_info);
		mgw_data_release(stream_info);
	}

	mgw_data_release(dev_info);
	mgw_data_release(source_info);
	return ret;
}

/**< Stop all output streams what is in this source stream, and stop this source stream */
void MGWApp::StopSourceStream(mgw_data_t *settings)
{
	mgw_data_t *dev_info = mgw_data_get_obj(settings, "device");
	mgw_data_t *source_info = mgw_data_get_obj(settings, "stream");

	mgw_device_t *device = NULL;
	const char *dev_sn = mgw_data_get_string(dev_info, "sn");
	if (!(device = mgw_get_weak_device_by_name(dev_sn))) {
		tlog(TLOG_ERROR, "Couldn't find device:%s", dev_sn);
		return;
	}

	int channel = mgw_data_get_int(source_info, "src_channel");
	std::string stream_name = "stream" + std::to_string(channel);
	mgw_device_release_stream(device, stream_name.data());

	mgw_data_release(dev_info);
	mgw_data_release(source_info);
}

int MGWApp::GetOutputStreamInfo(mgw_data_t *settings, mgw_data_t **result)
{

}

int MGWApp::GetSourceStreamInfo(mgw_data_t *settings, mgw_data_t **result)
{

}

static void show_usage(void)
{
	char buffer[1024] = {}, *opts = buffer;
	opts += sprintf(opts, "Usage: ./install/bin/mgw [options]\n");
	opts += sprintf(opts, "Options:\n");
	opts += sprintf(opts, "  -h              Display this help\n");
	opts += sprintf(opts, "  -v              Display version information\n");
	opts += sprintf(opts, "  -c filename     Specify a file to start the server\n");
	fprintf(stdout, "%s", buffer);
}

/**< Get configuration file name with absolute path */
/**< Return true if normal, should run continue */
static bool parse_options(int argc, char *argv[], char **config_file)
{
	if (argc < 2) {
		fprintf(stdout, "Use default configuration file to start server\n");
		return true;
	}
	if (!strncmp(argv[1], "-h", 2)) {
		show_usage();
		return false;
	} else if (!strncmp(argv[1], "-v", 2)) {
		fprintf(stdout, "%s\n", mgw_get_version_string());
		return false;
	} else if (!strncmp(argv[1], "-c", 2) && argc == 3) {
		*config_file = bstrdup(argv[2]);
		return true;
	}
	fprintf(stdout, "argv[1]:%s\n", argv[1]);
	fprintf(stdout, "Error parameter, you can start with the following usage:\n");
	show_usage();
	return false;
}

static int events_loop(const MGWEvents &e)
{
	int err_code = 0;

	os_set_thread_name("mgw: main thread");
	while (getchar() != 'q') {
		usleep(10 * 1000);
	}

	return err_code;
}

int main(int argc, char *argv[])
{
	const char *exec_path = os_get_exec_path();
	if (!exec_path) {
		tlog(TLOG_FATAL, "Tired to get exec path failed!");
		return mgw_err_status::MGW_ERR_BAD_PATH;
	}
	const char *process_name = os_get_process_name();
	if (!process_name) {
		tlog(TLOG_FATAL, "Tried to get process name failed!");
		return mgw_err_status::MGW_ERR_BAD_PATH;
	}

	std::string name = std::string(process_name) + ".log";
	struct tlog_config log_cfg = {
		.block		= 1,
		.multiwrite	= 0,
		.max_size	= 2 * 1024 * 1024,	//2M
		.max_count	= 10,
		.buffer_size = 0,	//Do not buffer,log it directly
		.filename	= name.data()
	};
	tlog_init(&log_cfg);

	char *config_file = NULL;
	if (!parse_options(argc, argv, &config_file))
		return 0;

	std::string mgw_config(config_file);
	if (mgw_config.empty())
		std::string(exec_path) + "/mgw-config.json";
	if (!os_file_exists(mgw_config.data())) {
		tlog(TLOG_ERROR, "Haven't configuration file!");
		return mgw_err_status::MGW_ERR_BAD_PATH;
	}

	tlog(TLOG_INFO, "----------------  mgw startup ------------------");
	MGWApp &app = MGWApp::GetInstance();
	if (!app.Startup(mgw_config)) {
		tlog(TLOG_ERROR, "Tried to start app with "\
				"configuration file:%s failed!", mgw_config.data());
		return mgw_err_status::MGW_ERR_ESTARTING;
	}

#ifdef CONFIG_MESSAGE
	int result = 0;
	Message &msg = Message::GetInstance();

	mgw_data_t *mgwConfig = mgw_data_create_from_json_file(mgw_config.data());
	if (mgwConfig) {
		mgw_data_t *serverConfig = mgw_data_get_obj(mgwConfig, "server-config");
		result = msg.Register(serverConfig);
		mgw_data_release(serverConfig);
	} else {
		std::string serverConfig = std::string(exec_path) + "server-config.json";
		result = msg.Register(serverConfig);
	}

	mgw_data_release(mgwConfig);
	if (msg_status::MSG_STATUS_SUCCESS != result) {
		tlog(TLOG_ERROR, "Register api and message server failed, result:%d", result);
		return mgw_err_status::MGW_ERR_EMESSAGE_REGISTER;
	}
#endif

	if (!app.AttempToReset())
		tlog(TLOG_INFO, "Attemp to reset all settings failed!\n");

	MGWEvents e;
	int code = events_loop(e);

error:
	bfree((void *)exec_path);
	bfree((void*)process_name);
	tlog(TLOG_DEBUG, "------------  mgw shutdown code(%d) ------------", code);
	return code;
}