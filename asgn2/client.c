#include <sys/time.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <arpa/inet.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include "protocol.h"
#include "mainloop.h"
#include "utils.h"
#include "log.h"
static struct cfg {
	char addr[40];
	char filename[PATH_MAX];
	short port;
	int recv_win;
	unsigned int seed;
	double drop_rate;
	double read_rate;
}cfg;
static pthread_t reader;
//Probability send/receive. drop packet at cfg.drop_rate
ssize_t prob_send(int fd, uint8_t *buf, int len, int flags) {
	int tmp = rand_r(&cfg.seed);
	if (tmp < cfg.drop_rate*RAND_MAX) {
		log_info("\n[send] Following packet dropped: \n");
		protocol_print(buf, "\t", 1);
		return 0;
	}
	log_info("\n[send] Following packet sent: \n");
	protocol_print(buf, "\t", 1);
	return send(fd, buf, len, flags);
}
ssize_t prob_recv(int fd, uint8_t *buf, int len, int flags) {
	while(1) {
		int ret = recv(fd, buf, len, flags);
		int tmp;
		tmp = rand_r(&cfg.seed);
		if (ret < 0) {
			if (errno == EAGAIN)
				return 0;
			return ret;
		}
		if (tmp >= cfg.drop_rate*RAND_MAX) {
			log_info("\n[recv]Received: \n");
			protocol_print(buf, "\t", 1);
			return ret;
		}
		log_info("\n[recv] Following packet dropeed: \n");
		protocol_print(buf, "\t", 1);
	}
}
void *reader_thread(void *d) {
	struct protocol *p = d;
	uint8_t dbuf[DATAGRAM_SIZE*10];
	while(1) {
		struct timespec start, rt, end;
		long int tmpi;
		int ret;
		double tmpf;
		tmpi = random();
		tmpf = (double)tmpi/(double)RANDOM_MAX;
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
			log_info("[reader_thread DATA]\n%s\n", dbuf);
		}
		if (tc > 0) {
			double st = end.tv_sec+(double)end.tv_nsec/1000000000.0;
			log_info("[reader_thread] slept for %lf milliseconds "
			    "(%lf expected).\n", st*1000, (double)tmpf);
			log_info("[reader_thread] read %d datagrams "
			    "(%d bytes) in total.\n", tc, tb);
		} else
			log_info("[reader_thread] No data read\n");
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
		return;
	}
	if (err == -ETIMEDOUT) {
		log_warning("Connection timedout, quitting...\n");
		exit(1);
	}
	if (err < 0) {
		log_warning("Unhandled error: %s\n", strerror(err));
		exit(1);
	}
	if (err > 0) {
		if (p->state == CLOSED) {
			void *status;
			log_info("Connection terminated, waiting for "
			    "reader thread to finish...\n");
			pthread_join(reader, &status);
			log_info("Quitting...\n");
			exit(0);
		}
		log_debug("Unhandled status change\n");
	}
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

	log_info("Interface info:\n");
	dump_ifi_info(AF_INET, 1);
	struct sockaddr_in saddr;
	struct sockaddr_in laddr;
	struct sockaddr *laddrp = (struct sockaddr *)&laddr;
	int flags = 0, ret;
	char *iptmp = NULL;
	size_t len = 0;
	inet_pton(AF_INET, cfg.addr, &saddr.sin_addr);
	saddr.sin_port = htons(cfg.port);
	saddr.sin_family = AF_INET;
	ret = islocal_addr(&saddr, &laddr);
	if (ret == 2) {
		log_info("Server address is same machine\n");
		log_info("\tUse server ip: 127.0.0.1\n");
		log_info("\tUse client ip: 127.0.0.1\n");
		inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
		inet_pton(AF_INET, "127.0.0.1", &laddr.sin_addr);
		flags = MSG_DONTROUTE;
	} else if (ret == 1) {
		log_info("Server address is in local network\n");
		log_info("\tUse server ip: %s\n",
		    sa_ntop((struct sockaddr *)&saddr, &iptmp, &len));
		log_info("\tUse client ip: %s\n",
		    sa_ntop((struct sockaddr *)&laddr, &iptmp, &len));
		flags = MSG_DONTROUTE;
	} else {
		socklen_t llen;
		int tmpfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (tmpfd < 0) {
			log_warning("Failed to create socket, %s\n",
			    strerror(errno));
			return 1;
		}
		ret = connect(tmpfd, (struct sockaddr *)&saddr, sizeof(saddr));
		if (ret < 0) {
			log_warning("Failed to create socket, %s\n",
			    strerror(errno));
			return 1;
		}

		getsockname(tmpfd, (struct sockaddr *)&laddr, &llen);
		laddrp = NULL;
		close(tmpfd);
		log_info("Server address is no local\n");
		log_info("\tUse server ip: %s\n",
		    sa_ntop((struct sockaddr *)&saddr, &iptmp, &len));
		log_info("\tUse client ip: %s\n",
		    sa_ntop((struct sockaddr *)&laddr, &iptmp, &len));
	}

	void *ml = mainloop_new();
	protocol_connect(ml,
	    (struct sockaddr *)&saddr, sizeof(saddr),
	    laddrp, sizeof(laddr), flags, cfg.filename, cfg.recv_win,
	    prob_send, prob_recv, connect_callback);

	mainloop_run(ml);
	free(ml);
	return 0;
}
