/**< C++ header files */
#include "mgw-app.h"
#include "message.h"
#include "mgw-events.h"

/**< C header files */
extern "C" {
#include "util/base.h"
#include "util/tlog.h"
#include "util/mgw-data.h"
#include "util/platform.h"
#include "formats/ff-demuxing.h"
#include <stdio.h>
#include <unistd.h>
}

// #define CONFIG_MESSAGE			1

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

/**< Get configuration file file name with absolute path */
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

	while (getchar() != 'q') {
		usleep(10 * 1000);
	}

	return err_code;
}

int main(int argc, char *argv[])
{
	const char *exec_path = os_get_exec_path();
	if (!exec_path) {
		tlog(TLOG_FATAL, "Tired to get exec path failed!\n");
		return mgw_err_status::MGW_ERR_BAD_PATH;
	}
	const char *process_name = os_get_process_name();
	if (!process_name) {
		tlog(TLOG_FATAL, "Tried to get process name failed!\n");
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
		tlog(TLOG_ERROR, "Haven't configuration file!\n");
		return mgw_err_status::MGW_ERR_BAD_PATH;
	}

	tlog(TLOG_INFO, "----------------  mgw startup ------------------");
	MGWApp &app = MGWApp::GetInstance();
	if (!app.Startup(mgw_config)) {
		tlog(TLOG_ERROR, "Tried to start app with "\
				"configuration file:%s failed!\n", mgw_config.data());
		return mgw_err_status::MGW_ERR_ESTARTING;
	}

#ifdef CONFIG_MESSAGE
	std::string server_config = std::string(exec_path) + "";
	Message &msg = Message::GetInstance();
	if (msg_status::MSG_STATUS_SUCCESS !=
		msg.Register("")) {
		tlog(TLOG_ERROR, "Register api and message server failed!");
		code = mgw_err_status::MGW_ERR_EMESSAGE_REGISTER;
		goto error;
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