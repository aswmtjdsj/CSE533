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
