#ifndef _APP_MGW_APP_H_
#define _APP_MGW_APP_H_

#include <string>
#include <unordered_map>

#include "mgw.h"

class MGWApp
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

private:
	MGWApp();
	MGWApp(const MGWApp &) = delete;
	MGWApp(const MGWApp &&) = delete;
	MGWApp operator=(const MGWApp &) = delete;


	/**< Data */
	bool    started_;
	std::unordered_map<std::string, mgw_device_t> devices_;
};

#endif  //_APP_MGW_APP_H_