#ifndef _MESSAGE_DOWNLOAD_H_
#define _MESSAGE_DOWNLOAD_H_

extern "C" {
#include "curl/curl.h"
#include "util/threading.h"
}

#include <string>
#include <mutex>
#include <map>

#if !defined(CROSS_PLATFORM)
    #include <thread>
#else
    #include <pthread.h>
#endif

typedef enum dl_err {
    TASK_EOK = 0,
    /* curl error code ...  */
    TASK_EDL = CURL_LAST+1,
    TASK_EPATH,
    TASK_EFILE,
    TASK_ERESUME,
}task_err_t;

typedef struct dlm_task_ctx {
    bool            head_only;
    bool            bp_resume;
    bool            dl_section;
    uint64_t        cb_frequency;   //ms
    uint64_t        delay;
    char            *url;
    char            *filename;
    char            *cache_path;
    char            *netif_name;

    void (*task_cb)(CURL* task_handle, double dlnow, double dltotal, task_err_t err_code, int http_code);
}task_ctx_t;

class DlTask;

class Downloader
{
public:
	Downloader(uint8_t taskLimit = 10);
	~Downloader();

	void *AddTask(task_ctx_t *taskCtx)const;
	bool DelTask(void *handle);
	bool PauseTask(void *handle);
	bool ResumeTask(void *handle);
	bool HasTask(const std::string *uri);
	void DoTask(void);

protected:
	static void *TaskThread(void *arg);

private:
	int CURLMultiSelect(void);

private:
	bool threadStart_;
    uint8_t taskLimit_;
    os_sem_t	*taskSem_;
    std::mutex taskMutex_;
    CURLM *multiCurl_;
    std::map<CURL*, std::shared_ptr<DlTask>> tasks_;

#if !defined(CROSS_PLATFORM)
    std::shared_ptr<std::thread> threadPtr_;
#else
    pthread_t thread_;
#endif
};

#endif  //_MESSAGE_DOWNLOAD_H_