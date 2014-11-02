#include <sys/time.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>
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
static pthread_t reader;
//Probability send/receive. drop packet at cfg.drop_rate
ssize_t prob_send(int fd, uint8_t *buf, int len, int flags) {
	return send(fd, buf, len, flags);
}
ssize_t prob_recv(int fd, uint8_t *buf, int len, int flags) {
	return recv(fd, buf, len, flags);
}
void *reader_thread(void *d) {
	struct random_data buf;
	struct protocol *p = d;
	uint8_t dbuf[DATAGRAM_SIZE*10];
	char *sbuf = malloc(128);
	memset(sbuf, 0, 128);
	memset(&buf, 0, sizeof(buf));
	int ret = initstate_r(cfg.seed, sbuf, 100, &buf);
	if (ret != 0) {
		log_warning("[reader_thread] srandom_r failed, %d\n",
		    ret);
		perror("");
		return NULL;
	}
	while(1) {
		struct timespec start, rt, end;
		int tmpi;
		double tmpf;
		random_r(&buf, &tmpi);
		tmpf = (double)tmpi/(double)RAND_MAX;
		tmpf = -1*cfg.read_rate*log(tmpf);
		clock_gettime(CLOCK_MONOTONIC, &start);
		rt = start;
		rt.tv_nsec += tmpf*1000000;
		rt.tv_sec += rt.tv_nsec/1000000000;
		rt.tv_nsec %= 1000000000;
nsleep:
		//log_info("[read_thread] Now going back to sleep\n");
		ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
				      &rt, NULL);
		if (ret < 0) {
			if (errno != EINTR) {
				log_warning("[reader_thread] failed to sleep, "
				    "%s\n", strerror(errno));
				return NULL;
			}
			goto nsleep;
		}

		clock_gettime(CLOCK_MONOTONIC, &end);
		timespec_substract(&end, &start);
		//double st = end.tv_sec+(double)end.tv_nsec/1000000000.0;
		//log_info("[reader_thread] slept for %lf milliseconds "
		//    "(%lf expected).\n", st*1000, (double)tmpf);
		int count, tc = 0, tb = 0;
		while(1) {
			count = 10;
			ret = protocol_read(p, dbuf, &count);
			if (count == 0) {
				if (ret < 0) {
					log_info("[reader_thread] Connection "
					    "terminated, reader_thread quiting"
					    "...\n");
					return NULL;
				}
				break;
			}
			tc += count;
			tb += ret;
			dbuf[ret] = 0;
			log_info("[DATA] %s", dbuf);
		}
		//log_info("[reader_thread] read %d datagrams "
		//    "(%d bytes) in total.\n", tc, tb);
	}
	return NULL;
}
void data_callback(struct protocol *p, int nm) {
	uint8_t *buf = malloc((DATAGRAM_SIZE-HDR_SIZE)*nm);
	ssize_t ret = protocol_read(p, buf, &nm);
	buf[ret] = 0;
	log_info("[DATA %d datagrams, %zd bytes]: %s\n", nm, ret, buf);
	free(buf);
}
void connect_callback(struct protocol *p, int err) {
	if (err == 0) {
#if 0
		//Use a callback to read data
		p->dcb = data_callback;
		//Or insert a timer to read data
		void *th = timer_insert(xxxxx);
#endif
		//Use a separate thread to read data;
		pthread_create(&reader, NULL, reader_thread, p);
		pthread_detach(reader);
		return;
	}
	if (err == ETIMEDOUT) {
		log_warning("Connection timedout, quiting...\n");
		exit(1);
	}
	log_warning("Unhandled error: %s\n", strerror(err));
	exit(1);
}
int main(int argc, char * const *argv) {
	const char *cfgname;
	if (argc >= 2)
		cfgname = argv[1];
	else
		cfgname = "client.in";
	FILE *cfgfile = fopen(cfgname, "r");
	if (!cfgfile) {
		log_warning("Failed to open %s\n", cfgname);
		return 1;
	}
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

	struct sockaddr_in saddr;
	int flags, ret;
	inet_pton(AF_INET, cfg.addr, &saddr.sin_addr);
	saddr.sin_port = htons(cfg.port);
	saddr.sin_family = AF_INET;
	ret = islocal_addr(&saddr);
	if (ret == 2) {
		log_info("Server address is same machine\n");
		inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
		flags = MSG_DONTROUTE;
	} else if (ret == 1) {
		log_info("Server address is in local network\n");
		flags = MSG_DONTROUTE;
	}

	void *ml = mainloop_new();
	protocol_connect(ml, (struct sockaddr *)&saddr,
	    flags, cfg.filename, cfg.recv_win, prob_send, prob_recv,
	    connect_callback);

	mainloop_run(ml);
	free(ml);
	return 0;
}
