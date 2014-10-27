#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>

/*
 * debug = 9
 * info = 8
 * warning = 7
 * none = 0
 */

#if LOG_LEVEL >= 9
#define log_debug(...) fprintf(stderr, __VA_ARGS__)
#else
#define log_debug(...)
#endif

#if LOG_LEVEL >= 8
#define log_info(...) fprintf(stderr, __VA_ARGS__)
#else
#define log_info(...)
#endif

#if LOG_LEVEL >= 7
#define log_warning(...) fprintf(stderr, __VA_ARGS__)
#else
#define log_warning(...)
#endif

#endif
