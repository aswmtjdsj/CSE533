#include <stdlib.h>
#include <string.h>
#include "protocol.h"
struct protocol *protocol_new(void *ml, timer_cb cb) {
	struct protocol *x = malloc(sizeof(struct protocol));
	x->state = CLOSED;
	x->ml = ml;
	x->cb = cb;
	x->window_size = 10;
	return x;
}
