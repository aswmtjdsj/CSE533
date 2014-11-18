#pragma once
#include <stdint.h>
#include <stdlib.h>

#define ODR_MSG_STALE 1

struct odr_protocol;
struct msg {
	uint16_t flags;
	uint16_t len;
	void *buf;
	struct msg *next;
};

typedef void (*data_cb)(void *data);
int send_msg_api(struct odr_protocol *op, uint32_t dst_ip,
		 const char *buf, size_t len, int flags);
