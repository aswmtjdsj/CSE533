#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__
#include <sys/time.h>
#include <stdint.h>
#include <pthread.h>
#include "utils.h"
#include "mainloop.h"

#define DATAGRAM_SIZE 512

struct protocol;

typedef ssize_t (*send_func)(int fd, uint8_t *buf, int len, int flags);
typedef ssize_t (*recv_func)(int fd, uint8_t *buf, int len, int flags);
typedef void (*connect_cb)(struct protocol *, int);
typedef void (*data_cb)(struct protocol *, int);

struct seg {
	uint8_t buf[DATAGRAM_SIZE];
	int present;
	size_t len;
	void *timeout; /* Timer used for retransmit */
};

struct protocol {
	enum {
		SYN_SENT,
		ESTABLISHED,
		CLOSE_WAIT,
		CLOSED,
		LAST_ACK,
	}state;
	int fd, send_flags;
	int window_size;
	int syn_seq;
	void *ml, *fh, *timeout;
	send_func send;
	recv_func recv;
	connect_cb ccb;
	data_cb dcb;
	/* head and tail of the window */
	int h, t, e;
	int eseq, tseq;
	int syn_ack;
	struct seg *window;
	pthread_mutex_t window_lock;
	char *filename;
};

struct tcp_header {
	uint32_t seq;
	uint32_t ack;
	uint32_t tsopt, tsecr;
	uint16_t flags;
	uint16_t window_size;
};

#define HDR_FLAGS(n) (1<<(n))
#define HDR_SYN HDR_FLAGS(0)
#define HDR_ACK HDR_FLAGS(1)
#define HDR_FIN HDR_FLAGS(2)

#define HDR_SIZE (sizeof(struct tcp_header))

/* Who knows how the compiler gonna align this shit */
CASSERT(HDR_SIZE != 20, tcp_header_size);

void protocol_destroy(struct protocol *);

struct protocol *
protocol_connect(void *ml, struct sockaddr *saddr, int flags,
		 const char *filename, int recv_win, send_func sender,
		 recv_func recvf, connect_cb cb);

ssize_t protocol_read(struct protocol *p, uint8_t *buf, int *ndgram);
#endif
