#include <stdlib.h>
#include <string.h>
#include "protocol.h"
struct protocol *protocol_new(void *ml) {
	struct protocol *x = malloc(sizeof(struct protocol));
	x->state = CLOSED;
	memset(&x->timeout, 0, sizeof(x->timeout));
	x->ml = ml;
	return x;
}
void protocol_gen_syn(struct protocol *p, uint8_t **pkt, int *len) {
	p->state = SYN_SENT;
	p->timeout.tv_usec = 0;
	p->timeout.tv_sec = 3;
	if (*len < sizeof(struct tcp_header)) {
		*len = sizeof(struct tcp_header);
		free(*pkt);
		*pkt = malloc(sizeof(struct tcp_header));
	}
	struct tcp_header *hdr = (struct tcp_header *)pkt;
	hdr->
}
