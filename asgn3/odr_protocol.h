#pragma once
#include <stdint.h>
#include <stdlib.h>

#include "utils.h"

#define ODR_MSG_STALE 1

struct odr_protocol;
struct msg {
	uint16_t flags;
	uint16_t len;
	void *buf;
	struct msg *next;
};

typedef void (*data_cb)(void *data, uint16_t len);
int send_msg_api(struct odr_protocol *op, uint32_t dst_ip,
		 const char *buf, size_t len, int flags);
void *odr_protocol_init(void *ml, data_cb cb, int stale,
			struct ifi_info *head);
