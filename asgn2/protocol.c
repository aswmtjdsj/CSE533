#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
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

static int protocol_available_window(struct protocol *p) {
	int tmp = p->h-p->e;
	if (tmp < 0)
		tmp += p->window_size;
	return tmp;
}

static void make_header(uint32_t seq, uint32_t ack, uint16_t flags,
			uint16_t wsz, uint32_t ts, uint8_t *buf) {
	struct tcp_header *hdr = (void *)buf;
	hdr->ack = htonl(ack);
	hdr->seq = htonl(seq);
	hdr->flags = htons(flags);
	hdr->window_size = htons(wsz);
	hdr->tsecr = htonl(ts);

	//Set our own timestamp
	
	return;
}
static void protocol_data_callback(void *ml, void *data, int rw) {
	/* It's still possible to receive SYN-ACK here.
	 * And we need to re-transmit ACK if we do */
	struct protocol *p = data;
	uint8_t buf[DATAGRAM_SIZE], s[DATAGRAM_SIZE];
	uint16_t owsz = protocol_available_window(p);
	ssize_t ret = p->recv(p->fd, buf, sizeof(buf), 0);
	if (ret <= 0) {
		/* Possibly simulated data loss */
		if (ret < 0)
			log_warning("recv() failed: %s\n", strerror(errno));
		return;
	}

	if (ret < DATAGRAM_SIZE)
		log_info("Short packet: %d bytes\n", ret);

	size_t recv_size = ret;
	struct tcp_header *hdr = (void *)buf;
	hdr->window_size = ntohs(hdr->window_size);
	hdr->ack = ntohl(hdr->ack);
	hdr->seq = ntohl(hdr->seq);
	hdr->flags = ntohs(hdr->flags);
	hdr->tsopt = ntohl(hdr->tsopt);

	if ((hdr->flags & HDR_ACK) && (hdr->flags & HDR_SYN)) {
		/* We received an SYN-ACK, send ACK immediately */
		log_info("Duplicated SYN-ACK received, ");
		if (hdr->ack != p->syn_seq+1)
			log_info("ACK number invalid, discarding...\n");
		else {
			log_info("resending ACK...\n");
			make_header(hdr->ack, hdr->seq+1, HDR_ACK, owsz,
				    hdr->tsopt, s);
			p->send(p->fd, s, sizeof(*hdr), p->send_flags);
		}
		return;
	}

	if (hdr->seq < p->eseq) {
		log_info("Duplicated packet received, resending ACK\n");
		make_header(hdr->ack, p->eseq, HDR_ACK, owsz, hdr->tsopt, s);
		p->send(p->fd, s, sizeof(*hdr), p->send_flags);
		return;
	}
	log_info("Valid data packet received, seq %u, ack %u, size %u\n",
		 hdr->seq, hdr->ack, recv_size);
	if (hdr->seq >= p->tseq) {
		while(p->tseq <= hdr->seq) {
			p->window[p->t].present = 0;
			p->t = (p->t+1)%p->window_size;
			p->tseq++;
		}
	}

	int tmp = (p->e+hdr->seq-p->eseq)%p->window_size;
	p->window[tmp].len = recv_size-HDR_SIZE;
	memcpy(p->window[tmp].buf, hdr+1, recv_size-HDR_SIZE);
	p->window[tmp].present = 1;
	while(p->window[p->e].present && p->e != p->t) {
		p->e = (p->e+1)%p->window_size;
		p->eseq++;
	}
	make_header(hdr->ack, p->eseq, HDR_ACK,
		    protocol_available_window(p), hdr->tsopt, s);
	p->send(p->fd, s, sizeof(*hdr), p->send_flags);

	if (p->e != p->h && p->dcb) {
		int tmp = p->e-p->h;
		if (tmp < 0)
			tmp += p->window_size;
		p->dcb(p, tmp);
	}
	return;
}

static void protocol_synack_handler(void *ml, void *data, int rw) {
	struct protocol *p = data;
	uint8_t buf[DATAGRAM_SIZE];
	ssize_t ret = p->recv(p->fd, buf, sizeof(buf), 0);
	if (ret <= 0) {
		/* Possibly simulated data loss */
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
	if (hdr->ack != p->syn_seq+1) {
		log_warning("Wrong ack number %d (%d expected), "
		    "discard packet\n", hdr->ack, p->syn_seq+1);
		return;
	}

	//Verify flags
	if (!(hdr->flags & HDR_SYN) || !(hdr->flags & HDR_ACK)) {
		log_warning("Wrong packet type %d (SYN-ACK expected), "
		    "discard packet\n", hdr->flags);
		return;
	}

	log_info("Packet valid, continuing...\n");
	//Packet is valid, stop the timer
	timer_remove(p->ml, p->timeout);
	p->timeout = NULL;

	//Record the seq number
	p->tseq = p->eseq = hdr->seq+1;

	//New port number
	uint16_t *nport_p = (void *)(hdr+1);
	uint16_t nport = ntohs(*nport_p);

	struct sockaddr _saddr;
	socklen_t len = sizeof(_saddr);
	getsockname(p->fd, &_saddr, &len);
	assert(_saddr.sa_family == AF_INET);

	//Reconnect
	struct sockaddr_in *saddr = (struct sockaddr_in *)&_saddr;
	saddr->sin_port = htons(nport);
	ret = connect(p->fd, (struct sockaddr *)saddr, len);
	if (ret < 0) {
		log_warning("Failed to re-connect: %s\n", strerror(errno));
		p->ccb(p, errno);
	}

	log_debug("Reconnected, port %u\n", nport);

	//Send ACK
	uint32_t tmp = hdr->ack;
	hdr->ack = htonl(hdr->seq+1);
	hdr->seq = htonl(tmp);
	hdr->flags = htons(HDR_ACK);
	hdr->window_size = htons(protocol_available_window(p));
	p->send(p->fd, buf, DATAGRAM_SIZE, p->send_flags);
	p->state = ESTABLISHED;
	fd_set_cb(p->fh, protocol_data_callback);
	log_debug("ACK sent\n");

	p->ccb(p, 0);
}

static void
protocol_syn_timeout(void *ml, void *data, const struct timeval *tv) {
	struct protocol *p = data;
	uint8_t *pkt = malloc(HDR_SIZE+strlen(p->filename));
	int len = HDR_SIZE+strlen(p->filename);

	//Connection failed
	if (tv->tv_sec >= 12) {
		log_warning("Maximum number of timedouts reached, "
		    "giving up...\n");
		p->ccb(p, ETIMEDOUT);
		protocol_destroy(p);
		return;
	}

	//Build syn packet
	struct tcp_header *hdr = (struct tcp_header *)pkt;
	hdr->seq = htonl(p->syn_seq);
	hdr->ack = 0;
	hdr->window_size = htons(protocol_available_window(p));
	hdr->flags = htons(HDR_SYN);
	memcpy(hdr+1, p->filename, strlen(p->filename));

	log_info("SYN/ACK timedout, resending SYN...\n");
	p->send(p->fd, pkt, len, p->send_flags);

	//Double the timeout
	struct timeval tv2;
	tv2.tv_sec = tv->tv_sec;
	tv2.tv_usec = 0;
	timer_add(&tv2, &tv2);
	timer_insert(p->ml, &tv2, protocol_syn_timeout, p);
	log_info("Next SYN timeout: %lfs\n", tv2.tv_sec+tv2.tv_usec/1e6);
}

struct protocol *
protocol_connect(void *ml, struct sockaddr *saddr, int send_flags,
		 const char *filename, int recv_win, send_func sendf,
		 recv_func recvf,  connect_cb cb) {
	struct protocol *p = protocol_new(ml);
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	int myflags = 0;
	p->fd = sockfd;

	int ret = connect(sockfd, (struct sockaddr *)saddr, sizeof(*saddr));
	if (ret < 0) {
		log_warning("Failed to connect: %s\n", strerror(errno));
		return NULL;
	}

	ret = fcntl(sockfd, F_SETFL, O_NONBLOCK|O_RDWR);
	if (ret < 0) {
		log_warning("Failed to set non blocking: %s\n", strerror(errno));
		close(sockfd);
		return NULL;
	}

	int len = HDR_SIZE+strlen(filename);
	uint8_t *pkt = malloc(len);
	p->window_size = recv_win;
	p->send = sendf;
	p->recv = recvf;
	p->filename = strdup(filename);
	p->send_flags = send_flags;
	p->ccb = cb;
	p->dcb = NULL;
	p->window = calloc(p->window_size+1, sizeof(struct seg));
	p->h = p->t = p->e = 0;

	//Build syn packet
	struct tcp_header *hdr = (struct tcp_header *)pkt;
	hdr->seq = random();
	p->syn_seq = ntohl(hdr->seq);
	hdr->ack = 0;
	hdr->window_size = htons(protocol_available_window(p));
	hdr->flags = htons(HDR_SYN);
	memcpy(hdr+1, filename, strlen(filename));

	sendf(sockfd, pkt, len, myflags);
	free(pkt);

	//Change state
	p->state = SYN_SENT;
	struct timeval tv;
	tv.tv_sec = 3;
	tv.tv_usec = 0;
	p->timeout = timer_insert(p->ml, &tv, protocol_syn_timeout, p);
	p->fh = fd_insert(ml, sockfd, FD_READ, protocol_synack_handler, p);

	return p;
}

void protocol_destroy(struct protocol *p) {
	fd_remove(p->ml, p->fh);
	close(p->fd);
	free(p->filename);
	free(p);
}

int protocol_read(struct protocol *p, uint8_t *buf, int ndgram) {
	uint8_t *tmp = buf;
	int count = 0;
	while((p->h != p->e) && count < ndgram) {
		memcpy(tmp, p->window[p->h].buf, DATAGRAM_SIZE-HDR_SIZE);
		tmp += DATAGRAM_SIZE-HDR_SIZE;
		p->h = (p->h+1)%p->window_size;
		count++;
	}
	return count;
}
