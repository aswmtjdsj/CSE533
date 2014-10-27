#include <sys/time.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <arpa/inet.h>
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
static void *ml;
static struct protocol *p;
void timer_callback(void *ml, void *data, const struct timeval *elapsed) {
	double e = elapsed->tv_sec;
	struct timeval *xx = data;
	double e2 = xx->tv_sec;
	e2 += ((double)xx->tv_usec)/1e6;
	e += ((double)elapsed->tv_usec)/1e6;
	fprintf(stderr, "expected %lf, real %lf\n", e2, e);
	free(data);
}
static void
connect_to_server(void) {
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	int myflags = 0;
	struct sockaddr_in saddr;
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
	uint8_t *pkt = NULL;
	int len = 0;
	p = protocol_new();
	protocol_gen_syn(p, &pkt, &len);
	sendto(sockfd, pkt, len, myflags,
	       (struct sockaddr *)&saddr, sizeof(saddr));
}
int main(int argc, char * const *argv) {
	const char *cfgname;
	if (argc >= 2)
		cfgname = argv[1];
	else
		cfgname = "client.in";
	FILE *cfgfile = fopen(cfgname, "r");

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

	connect_to_server();

	ml = mainloop_new();
	struct timeval *tv = malloc(sizeof(struct timeval));
	tv->tv_sec = 2;
	tv->tv_usec = 2000;
	timer_insert(ml, tv, timer_callback, tv);
	tv = malloc(sizeof(struct timeval));
	tv->tv_sec = 2;
	tv->tv_usec = 4100;
	timer_insert(ml, tv, timer_callback, tv);
	mainloop_run(ml);
	free(ml);
}
