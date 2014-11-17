#pragma once
#include <stdint.h>

#define ODR_MSG_STALE 1

struct odr_protocol;
struct msg {
	uint16_t flags;
	uint16_t len;
	void *buf;
	struct msg *next;
};

typedef void (*data_cb)(void *data);
int send_msg(struct odr_protocol *op, struct msg *msg);
