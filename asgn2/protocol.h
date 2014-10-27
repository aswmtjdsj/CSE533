#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__
#include <sys/time.h>
#include <stdint.h>
#include "utils.h"
#include "mainloop.h"

struct protocol {
	enum {
		SYN_SENT,
		ESTABLISHED,
		CLOSE_WAIT,
		CLOSED,
		LAST_ACK,
	}state;
	int fd;
	int window_size;
	void *ml;
	struct timeval timeout;
};

struct tcp_header {
	uint32_t seq;
	uint32_t ack;
	uint16_t flags;
	uint16_t window_size;
};

CASSERT(sizeof(struct tcp_header) == 96, tcp_header_size);

struct protocol *protocol_new(void *ml, timer_cb);
void protocol_gen_syn(struct protocol *, uint8_t **, int *);
#endif
