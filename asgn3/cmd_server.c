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
	char *name;
	int fd;
	void *fh;
	struct client *next, **np;
}*head;
void register_cb(void *ml, void *data, int rw) {
	struct client *cdata = data;
	char buf[1024];
	int ret = recv(cdata->fd, buf, sizeof(buf), 0);
	if (ret < 0) {
		fd_remove(ml, cdata->fh);
		close(cdata->fd);
		*(cdata->np) = cdata->next;
		free(cdata->name);
		free(cdata);
		return;
	}
	char *tmp = strchr(buf, ' ');
	if (tmp) {
		log_warn("Invalid name, contaions space\n");
		return;
	}
	memcpy(cdata->name, buf, ret);
	cdata->name[ret] = 0;
	fd_remove(ml, cdata->fh);
}
void listen_cb(void *ml, void *data, int rw) {
	int sockfd = fd_get_fd(data);
	struct client *nc = malloc(sizeof(struct client));
	socklen_t clen = sizeof(nc->caddr);
	int cfd = accept(sockfd, (struct sockaddr *)&nc->caddr, &clen);
	nc->fd = cfd;
	nc->fh = fd_insert(ml, cfd, FD_READ, register_cb, nc);
	nc->next = head;
	nc->np = &head;
	head = nc;
}
void cmd_cb(void *ml, void *data, int rw) {
	char cmd[2050];
	fgets(cmd, sizeof(cmd), stdin);
	chomp(cmd);
	char *name2 = strchr(cmd, ' ');
	if (!name2) {
		log_warn("Invalid command\n");
		return;
	}
	*name2=0;
	struct client *t = head;
	while(t){
		if (strcmp(cmd, t->name) == 0)
			break;
		t = t->next;
	}
	if (!t) {
		log_warn("Client %s not found\n", cmd);
		return;
	}
	int ret = write(t->fd, name2, strlen(name2));
	if (ret < 0)
		log_warn("Failed to send command: %s\n",
		    strerror(errno));
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

	void *sfh = fd_insert(ml, fileno(stdin), FD_READ, cmd_cb, NULL);
	fd_set_data(sfh, sfh);

	mainloop_run(ml);
	return 0;
}
