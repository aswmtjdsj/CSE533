#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__
#include <sys/time.h>
#include <stdint.h>
#include "utils.h"
#include "mainloop.h"

#define DATAGRAM_SIZE 512

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

/* protocol_new: build a new protocol structure
 * ml: the mainloop
 * timer_cb: the timeout_handler, will be called when timeout happens
 */
struct protocol *protocol_new(void *ml, timer_cb);

/* protocol_gen_syn: generate a packet without data,
 * can be used for sending SYN/ACK/FIN/etc.
 *
 * p: the protocol struct
 * buf: pointer to the buf
 * len: pointer to the len
 */
void protocol_gen_nodata(struct protocol *p, uint8_t **buf, int *len);

/* protocol_syn_sent: notify that a syn packet is sent
 * this will cause the a state change to SYN_SENT, and a
 * timer will be inserted into the mainloop.
 */
void protocol_syn_sent(struct protocol *p);
#endif
