#pragma once

#define ODR_MSG_STALE 1

typedef void (*data_cb)(void *data);
int send_msg(struct odr_protocol *op, struct msg *msg);
