#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include "utils.h"
char tmp[1600];
int main(){
	int fd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_ALL));
	if (fd < 0) {
		perror("Failed to create socket");
		exit(1);
	}
	struct sockaddr_ll addr;
	socklen_t len = sizeof(addr);
	while(recvfrom(fd, tmp, sizeof(tmp), 0, (struct sockaddr *)&addr, &len)) {
		dump_lladdr(&addr);
	}
}
