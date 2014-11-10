#include "odr_hdr.h"
#include "mainloop.h"
struct odr_protocol {
	int fd;
	void *fh;
};
void odr_read_cb(void *ml, void *data, int rw){
	struct odr_protocol *op = data;
}
void odr_protocol_init(void *ml) {
	int sockfd = socket(AF_PACKET, SOCK_DGRAM, htons(ODR_MAGIC));
	struct odr_protocol *op = malloc(sizeof(struct odr_protocol));
	op->fd = sockfd;
	op->fh = fd_insert(ml, sockfd, FD_READ, odr_read_cb, op);
}
