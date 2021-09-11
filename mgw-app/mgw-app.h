#ifndef _APP_MGW_APP_H_
#define _APP_MGW_APP_H_

#include <string>
#include <unordered_map>

#include "mgw-internal.h"

class MGWApp final
{
public:
	static MGWApp &GetInstance() {
		static MGWApp app;
		return app;
	}
	~MGWApp();

	bool Startup(const std::string &configFile);
	void ExitApp(void);
	bool AttempToReset(void);
	std::string &&GetAPIVersion(void);

	/**< Functions */
	int StartOutputStream(mgw_data_t *settings, mgw_data_t **result = NULL);
	void StopOutputStream(mgw_data_t *settings);

	int StartSourceStream(mgw_data_t *settings, mgw_data_t **result = NULL);
	void StopSourceStream(mgw_data_t *settings);

	int GetOutputStreamInfo(mgw_data_t *settings, mgw_data_t **result);
	int GetSourceStreamInfo(mgw_data_t *settings, mgw_data_t **result);

private:
	MGWApp();
	MGWApp(const MGWApp &) = delete;
	MGWApp(const MGWApp &&) = delete;
	MGWApp operator=(const MGWApp &) = delete;


private:
	/**< Data */
	bool    started_;
	std::unordered_map<std::string, mgw_device_t> devices_;
};

#endif  //_APP_MGW_APP_H_