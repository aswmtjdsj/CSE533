#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "mainloop.h"
#include "log.h"
#include "utils.h"
struct client {
	struct sockaddr_in caddr;
	int fd;
	void *fh;
};
void log_cb(void *ml, void *data, int rw) {
	struct client *cdata = data;
	char buf[1024];
	int ret = recv(cdata->fd, buf, sizeof(buf), MSG_PEEK);
	if (ret == 0) {
		close(cdata->fd);
		fd_remove(ml, cdata->fh);
		return;
	}
	int x = ret-1;
	buf[ret] = 0;
	while(buf[x] != '\n' && x>=0)
		x--;
	if (x == -1)
		x = ret-1;
	ret = read(cdata->fd, buf, x+1);
	buf[x+1] = 0;

	static char *tmp = NULL;
	static size_t len = 0;
	log_info("[IP: %s]\n%s", sa_ntop((struct sockaddr *)&cdata->caddr,
				       &tmp, &len), buf);
}
void listen_cb(void *ml, void *data, int rw) {
	int sockfd = fd_get_fd(data);
	struct client *nc = malloc(sizeof(struct client));
	socklen_t clen = sizeof(nc->caddr);
	int cfd = accept(sockfd, (struct sockaddr *)&nc->caddr, &clen);
	nc->fd = cfd;
	nc->fh = fd_insert(ml, cfd, FD_READ, log_cb, nc);
}
int main(int argc, const char **argv) {
	if(argc < 2) {
		log_info("Usage: %s [port]\n", argv[0]);
		return 1;
	}
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(argv[1]));
	int ret = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		log_warn("Failed to bind(): %s\n", strerror(errno));
		return 1;
	}
	ret = listen(sockfd, 0);
	if (ret < 0) {
		log_warn("Failed to listen(): %s\n", strerror(errno));
		return 1;
	}

	void *ml = mainloop_new();
	void *fh = fd_insert(ml, sockfd, FD_READ, listen_cb, NULL);
	fd_set_data(fh, fh);

	mainloop_run(ml);
	return 0;
}

