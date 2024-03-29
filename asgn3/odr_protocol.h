#pragma once
#include <stdint.h>
#include <stdlib.h>

#include "utils.h"

#define ODR_MSG_STALE 1

struct odr_protocol;
struct msg {
	uint16_t len;
	void *buf;
	struct msg *next;
};

typedef void (*data_cb)(void *buf, uint16_t len, uint32_t src_ip,
	      void *user_data);
int send_msg_api(struct odr_protocol *op, uint32_t dst_ip,
		 const void *buf, size_t len, int flags);
void *odr_protocol_init(void *ml, data_cb cb, void *data, int stale);
