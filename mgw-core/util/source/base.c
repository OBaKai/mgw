
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "c99defs.h"
#include "base.h"
#include "bmem.h"

#define _DEBUG
#ifdef _DEBUG
static int log_output_level = LOG_DEBUG;
#else
static int log_output_level = LOG_INFO;
#endif

static int  crashing     = 0;
static void *log_param   = NULL;
static void *crash_param = NULL;

static void def_log_handler(int log_level, const char *format,
		va_list args, void *param)
{
	char out[4096];
	vsnprintf(out, sizeof(out), format, args);

	if (log_level <= log_output_level) {
		switch (log_level) {
		case LOG_DEBUG:
			fprintf(stdout, "[debug] %s\n", out);
			fflush(stdout);
			break;

		case LOG_INFO:
			fprintf(stdout, "[info] %s\n", out);
			fflush(stdout);
			break;

		case LOG_WARNING:
			fprintf(stdout, "[warning] %s\n", out);
			fflush(stdout);
			break;

		case LOG_ERROR:
			fprintf(stderr, "[error] %s\n", out);
			fflush(stderr);
		}
	}

	UNUSED_PARAMETER(param);
}

#ifdef _MSC_VER
#define NORETURN __declspec(noreturn)
#else
#define NORETURN __attribute__((noreturn))
#endif

NORETURN static void def_crash_handler(const char *format, va_list args,
		void *param)
{
	vfprintf(stderr, format, args);
	exit(0);

	UNUSED_PARAMETER(param);
}

static log_handler_t log_handler = def_log_handler;
static void (*crash_handler)(const char *, va_list, void *) = def_crash_handler;

void base_get_log_handler(log_handler_t *handler, void **param)
{
	if (handler)
		*handler = log_handler;
	if (param)
		*param = log_param;
}

void base_set_log_handler(log_handler_t handler, void *param)
{
	if (!handler)
		handler = def_log_handler;

	log_param   = param;
	log_handler = handler;
}

void base_set_crash_handler(
		void (*handler)(const char *, va_list, void *),
		void *param)
{
	crash_param   = param;
	crash_handler = handler;
}

void bcrash(const char *format, ...)
{
	va_list args;

	if (crashing) {
		fputs("Crashed in the crash handler", stderr);
		exit(2);
	}

	crashing = 1;
	va_start(args, format);
	crash_handler(format, args, crash_param);
	va_end(args);
}

void blogva(int log_level, const char *format, va_list args)
{
	log_handler(log_level, format, args, log_param);
}

void blog_ext(int log_level, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	blogva(log_level, format, args);
	va_end(args);
}

char *get_localtime_str(void)
{
    static char time_string[32] = {}; //bmalloc(32);
    time_t t;
    struct tm *tm;

    memset(time_string, 0, sizeof(time_string));

    time(&t);
    tm = localtime(&t);
    snprintf(time_string, sizeof(time_string), "%04d-%02d-%02d-%02d-%02d-%02d",\
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,\
                tm->tm_hour, tm->tm_min, tm->tm_sec);
    return time_string;
}