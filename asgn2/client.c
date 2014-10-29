#include <sys/time.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <arpa/inet.h>
#include <time.h>
#include "protocol.h"
#include "mainloop.h"
#include "utils.h"
#include "log.h"
static struct cfg {
	char addr[40];
	char filename[PATH_MAX];
	short port;
	int recv_win;
	int seed;
	double drop_rate;
	double read_rate;
}cfg;
void timer_callback(void *ml, void *data, const struct timeval *elapsed) {
	double e = elapsed->tv_sec;
	struct timeval *xx = data;
	double e2 = xx->tv_sec;
	e2 += ((double)xx->tv_usec)/1e6;
	e += ((double)elapsed->tv_usec)/1e6;
	fprintf(stderr, "expected %lf, real %lf\n", e2, e);
	free(data);
}
void timeout_handler(void *ml, void *data, const struct timeval *elapsed) {
}
void sock_read_handler(void *ml, void *data, int rw) {
}
int main(int argc, char * const *argv) {
	const char *cfgname;
	if (argc >= 2)
		cfgname = argv[1];
	else
		cfgname = "client.in";
	FILE *cfgfile = fopen(cfgname, "r");
	srandom(time(NULL));

	fgets(cfg.addr, sizeof(cfg.addr), cfgfile);
	if (!iseols(cfg.addr))
		err_quit("Server ip address too long\n", 1);
	chomp(cfg.addr);

	fscanf(cfgfile, "%hd\n", &cfg.port);

	fgets(cfg.filename, sizeof(cfg.filename), cfgfile);
	if (!iseols(cfg.filename))
		err_quit("filename longer than PATH_MAX\n", 1);
	chomp(cfg.filename);

	fscanf(cfgfile, "%d\n", &cfg.recv_win);

	fscanf(cfgfile, "%d\n", &cfg.seed);

	fscanf(cfgfile, "%lf\n", &cfg.drop_rate);

	fscanf(cfgfile, "%lf\n", &cfg.read_rate);

	void *ml = mainloop_new();

	inet_pton(AF_INET, cfg.addr, &saddr.sin_addr);
	saddr.sin_port = cfg.port;
	saddr.sin_family = AF_INET;
	if (islocal_addr(&saddr)) {
		struct sockaddr_in laddr;
		log_info("Server address is local\n");
		inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
		laddr.sin_family = AF_INET;
		bind(sockfd, (struct sockaddr *)&laddr, sizeof(laddr));
		myflags = MSG_DONTROUTE;
	}
	connect_to_server(ml);

	mainloop_run(ml);
	free(ml);
}
