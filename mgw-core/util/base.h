
#pragma once
#include <stdarg.h>
#include "c99defs.h"

/*
 * Just contains logging/crash related stuff
 */

#ifdef __cplusplus
extern "C" {
#endif

#define STRINGIFY(x) #x
#define STRINGIFY_(x) STRINGIFY(x)
#define S__LINE__ STRINGIFY_(__LINE__)

#define INT_CUR_LINE __LINE__
#define FILE_LINE __FILE__ " (" S__LINE__ "): "

enum {
	/**
	 * Use if there's a problem that can potentially affect the program,
	 * but isn't enough to require termination of the program.
	 *
	 * Use in creation functions and core subsystem functions.  Places that
	 * should definitely not fail.
	 */
	MGW_LOG_ERROR   = 100, 

	/**
	 * Use if a problem occurs that doesn't affect the program and is
	 * recoverable.
	 *
	 * Use in places where failure isn't entirely unexpected, and can
	 * be handled safely.
	 */
	MGW_LOG_WARNING = 200,

	/**
	 * Informative message to be displayed in the log.
	 */
	MGW_LOG_INFO    = 300,

	/**
	 * Debug message to be used mostly by developers.
	 */
	MGW_LOG_DEBUG   = 400
};

typedef void (*log_handler_t)(int lvl, const char *msg, va_list args, void *p);

EXPORT void base_get_log_handler(log_handler_t *handler, void **param);
EXPORT void base_set_log_handler(log_handler_t handler, void *param);

EXPORT void base_set_crash_handler(
		void (*handler)(const char *, va_list, void *),
		void *param);

EXPORT void blogva(int log_level, const char *format, va_list args);

#if !defined(_MSC_VER) && !defined(SWIG)
#define PRINTFATTR(f, a) __attribute__((__format__(__printf__, f, a)))
#else
#define PRINTFATTR(f, a)
#endif

PRINTFATTR(2, 3)
EXPORT void blog_ext(int log_level, const char *format, ...);
PRINTFATTR(1, 2)
EXPORT void bcrash(const char *format, ...);

EXPORT char *get_localtime_str(void);

#define blog(level, fmt, ...) blog_ext(level, "[%s][%s][%d] "fmt, \
                                get_localtime_str(), __FILE__, __LINE__, ##__VA_ARGS__)

#undef PRINTFATTR

#ifdef __cplusplus
}
#endif
