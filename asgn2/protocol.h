#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__
#include <sys/time.h>
#include <stdint.h>
#include "utils.h"
#include "mainloop.h"

#define DATAGRAM_SIZE 512

struct protocol;

typedef ssize_t (*send_func)(int fd, uint8_t *buf, int len, int flags);
typedef ssize_t (*recv_func)(int fd, uint8_t *buf, int len, int flags);
typedef void (*connect_cb)(struct protocol *, int);

struct seg {
	uint8_t buf[DATAGRAM_SIZE];
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
	int fd, flags;
	int window_size;
	//seq is the lowest un-ack'd packet's seq
	//ack is the lowest expected packet's seq
	int seq, ack;
	void *ml, *fh, *timeout;
	send_func send;
	recv_func recv;
	connect_cb cb;
	struct random_data buf;
	struct seg *window;
	char *filename;
};

struct tcp_header {
	uint32_t seq;
	uint32_t ack;
	uint16_t flags;
	uint16_t window_size;
};

#define HDR_FLAGS(n) (1<<(n))
#define HDR_SYN HDR_FLAGS(0)
#define HDR_ACK HDR_FLAGS(1)
#define HDR_FIN HDR_FLAGS(2)

#define HDR_SIZE (sizeof(struct tcp_header))

/* Who knows how the compiler gonna align this shit */
CASSERT(HDR_SIZE != 12, tcp_header_size);

void protocol_destroy(struct protocol *);

struct protocol *
protocol_connect(void *ml, struct sockaddr *saddr, int flags,
		 const char *filename, int recv_win, int seed,
		 send_func sender, recv_func recvf, connect_cb cb);
#endif
