#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include "protocol.h"
#include "log.h"
#define SYN_TIMEOUT_SEC (3)
static struct protocol *protocol_new(void *ml) {
	struct protocol *x = malloc(sizeof(struct protocol));
	x->state = CLOSED;
	x->ml = ml;
	x->window_size = 10;
	return x;
}

static void protocol_synack_handler(void *ml, void *data, int rw) {
	struct protocol *p = data;
	uint8_t buf[DATAGRAM_SIZE];
	ssize_t ret = p->recv(p->fd, buf, sizeof(buf), p->flags);
	if (ret <= 0) {
		//Possibly simulated data loss
		if (ret < 0)
			log_warning("recv() failed: %s\n", strerror(errno));
		return;
	}
	struct tcp_header *hdr = (void *)buf;
	hdr->window_size = ntohs(hdr->window_size);
	hdr->ack = ntohl(hdr->ack);
	hdr->seq = ntohl(hdr->seq);
	hdr->flags = ntohs(hdr->flags);

	//Verify ack number
	if (hdr->ack != p->seq+1) {
		log_warning("Wrong ack number %d (%d expected), "
		    "discard packet\n", hdr->ack, p->seq+1);
		return;
	}

	//Verify flags
	if (!(hdr->flags & HDR_SYN) || !(hdr->flags & HDR_ACK)) {
		log_warning("Wrong packet type %d (SYN-ACK expected), "
		    "discard packet\n", hdr->flags);
		return;
	}

	//Packet is valid, stop the timer
	timer_remove(p->ml, p->timeout);
	p->timeout = NULL;
	uint16_t *nport_p = (void *)(hdr+1);
	uint16_t nport = ntohs(*nport_p);

	struct sockaddr _saddr;
	socklen_t len = sizeof(_saddr);
	getsockname(p->fd, &_saddr, &len);
	assert(_saddr.sa_family == AF_INET);
	close(p->fd);

	//Reconnect
	struct sockaddr_in *saddr = (struct sockaddr_in *)&_saddr;
	saddr->sin_port = htons(nport);
	p->fd = socket(AF_INET, SOCK_DGRAM, 0);
	connect(p->fd, (struct sockaddr *)saddr, len);
}

static int protocol_available_window(struct protocol *p) {
	return 0;
}

static void
protocol_syn_timeout(void *ml, void *data, const struct timeval *tv) {
	struct protocol *p = data;
	uint8_t *pkt = malloc(HDR_SIZE+strlen(p->filename));
	int len = HDR_SIZE+strlen(p->filename);

	//Build syn packet
	struct tcp_header *hdr = (struct tcp_header *)pkt;
	hdr->seq = random();
	hdr->ack = 0;
	hdr->window_size = protocol_available_window(p);
	hdr->flags = HDR_SYN;
	memcpy(hdr+1, p->filename, strlen(p->filename));

	p->send(p->fd, pkt, len, p->flags);

	//Connection failed
	if (tv->tv_sec >= 12) {
		p->cb(p, ETIMEDOUT);
		protocol_destroy(p);
		return;
	}

	//Double the timeout
	struct timeval tv2;
	tv2.tv_sec = tv->tv_sec;
	tv2.tv_usec = 0;
	timer_add(&tv2, &tv2);
	timer_insert(p->ml, &tv2, protocol_syn_timeout, p);
}

/* protocol_syn_sent: notify that a syn packet is sent
 * this will cause the a state change to SYN_SENT, and a
 * timer will be inserted into the mainloop.
 */
static void protocol_syn_sent(struct protocol *p) {
	p->state = SYN_SENT;
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	p->timeout = timer_insert(p->ml, &tv, protocol_syn_timeout, p);
}

struct protocol *
protocol_connect(void *ml, struct sockaddr *saddr, int flags,
		 const char *filename, int recv_win, int seed,
		 send_func sendf, recv_func recvf,  connect_cb cb) {
	struct protocol *p = protocol_new(ml);
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	int myflags = 0;
	p->fd = sockfd;

	connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr));

	uint8_t *pkt = malloc(HDR_SIZE+strlen(filename));
	int len = HDR_SIZE+strlen(filename);
	p->window_size = recv_win;
	p->send = sendf;
	p->recv = recvf;
	p->filename = strdup(filename);
	p->flags = flags;
	p->cb = cb;
	p->window = calloc(p->window_size, sizeof(struct seg));
	srandom_r(seed, &p->buf);

	//Build syn packet
	struct tcp_header *hdr = (struct tcp_header *)pkt;
	hdr->seq = random();
	p->seq = hdr->seq;
	hdr->ack = 0;
	hdr->window_size = protocol_available_window(p);
	hdr->flags = HDR_SYN;
	memcpy(hdr+1, filename, strlen(filename));

	sendf(sockfd, pkt, len, myflags);
	free(pkt);

	protocol_syn_sent(p);
	p->fh = fd_insert(ml, sockfd, FD_READ, protocol_synack_handler, p);

	return p;
}

void protocol_destroy(struct protocol *p) {
	fd_remove(p->ml, p->fh);
	close(p->fd);
	free(p->filename);
	free(p);
}
