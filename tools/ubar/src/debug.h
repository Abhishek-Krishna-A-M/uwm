#ifndef UBAR_DEBUG_H
#define UBAR_DEBUG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static inline void ubar_log(const char *file, int line, const char *func, const char *fmt, ...) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	fprintf(stderr, "[%ld.%03ld] [%s:%d %s] ", ts.tv_sec, ts.tv_nsec / 1000000, file, line, func);
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
}

#define LOG(...) ubar_log(__FILE__, __LINE__, __func__, __VA_ARGS__)

#endif
