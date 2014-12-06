#include <arpa/inet.h>
#include <stdio.h>
#include "arp_client.h"
int main(int argc, const char **argv) {
	struct sockaddr_in sin;
	inet_pton(AF_INET, argv[1], &sin.sin_addr);
	sin.sin_family = AF_INET;
	struct hwaddr rep;
	areq((void *)&sin, sizeof(sin), &rep);

	int i;
	for (i = 0; i < rep.sll_halen; i++)
		printf("%02X ", rep.sll_addr[i]);
	printf("\n");
}
