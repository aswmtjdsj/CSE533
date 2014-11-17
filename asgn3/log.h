#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <stdarg.h>

#define LOG_LEVEL 10

/*
 * debug = 9
 * info = 8
 * warning = 7
 * none = 0
 */

static inline
int color_print(int log_level, const char *prefix, const char *postfix,
		const char *fmt, ...) {
	if (log_level > LOG_LEVEL)
		return 0;
	fputs(prefix, stderr);
	va_list args;
	va_start(args, fmt);
	int ret = vfprintf(stderr, fmt, args);
	va_end(args);
	fputs(postfix, stderr);
	return ret;
}

#define log_debug(...) \
    color_print(9, "\033[32m[DEBUG]", "\033[0m", __VA_ARGS__)
#define log_warn(...) \
    color_print(7, "\033[33m[WARN]", "\033[0m", __VA_ARGS__)
#define log_err(...) \
    color_print(6, "\033[31m[ERROR]", "\033[0m", __VA_ARGS__)
#define log_info(...) \
    color_print(8, "\033[32m[DEBUG]", "\033[0m", __VA_ARGS__)

static inline void log_server_init(int argc, const char **argv) {
	if (argc < 3)
		return;
	//Was supplied with a server to log to
	struct sockaddr_in logaddr;
	int ret = inet_pton(AF_INET, argv[1], &logaddr.sin_addr);
	int port = atoi(argv[2]);
	logaddr.sin_port = htons(port);
	logaddr.sin_family = AF_INET;
	if (ret != 1)
		return;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	ret = connect(fd,(struct sockaddr *)&logaddr,sizeof(logaddr));
	if (ret != 0) {
		log_info("Failed to connect to log server: "
		    "%s\n", strerror(errno));
		return;
	}
	ret = dup2(fd, fileno(stderr));
	if (ret < 0) {
		log_info("Failed to dup2(): %s\n", strerror(errno));
		return;
	}
}

#endif
