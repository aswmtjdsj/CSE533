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
	timer_cb cb;
	struct random_data buf;
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

/* protocol_new: build a new protocol structure
 * ml: the mainloop
 * timer_cb: the timeout_handler, will be called when timeout happens
 */
struct protocol *protocol_new(void *ml, timer_cb);

/* protocol_syn_sent: notify that a syn packet is sent
 * this will cause the a state change to SYN_SENT, and a
 * timer will be inserted into the mainloop.
 */
void protocol_syn_sent(struct protocol *p);

int protocol_available_window(struct protocol *);
#endif
