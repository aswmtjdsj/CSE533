#include <sys/un.h>
#include "arp_client.h"
#include "const.h"

int areq(struct sockaddr *addr, socklen_t len, struct hwaddr *rep) {
	if (addr->sa_family != AF_INET) {
		log_err("Unsupported address family\n");
		return -2;
	}
	if (len != sizeof(struct sockaddr_in)) {
		log_err("Invalid size\n");
		return -3;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);

	struct sockaddr_un addr_un;
	addr_un.sun_family = AF_UNIX;
	memset(addr_un.sun_path, 0, sizeof(addr_un.sun_path));
	strcpy(addr_un.sun_path+1, "areq_" ID);

	int ret = connect(fd, (void *)&addr_un, sizeof(addr_un));
	if (ret < 0) {
		log_err("Failed to connect %s\n",
		    strerror(errno));
		return -1;
	}

	struct sockaddr_in *addr_in = (void *)addr;
	ret = send(fd, &addr_in->sin_addr.s_addr, 4, 0);
	if (ret < 0) {
		log_err("Failed to send %s\n",
		    strerror(errno));
		return -1;
	}

	ret = recv(fd, rep, sizeof(struct hwaddr), 0);
	if (ret < 0) {
		log_err("Failed to recv %s\n",
		    strerror(errno));
		return -1;
	}
	close(fd);
	return 0;
}
