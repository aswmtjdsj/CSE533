#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "utils.h"

typedef unsigned char u_char;

int
main(int argc, char **argv)
{
	struct ifi_info *ifi, *ifihead;
	u_char *ptr;
	int i, family, doaliases;

	if (argc != 3)
		err_quit("usage: prifinfo_plus <inet4|inet6> <doaliases>", 1);

	if (strcmp(argv[1], "inet4") == 0)
		family = AF_INET;
	else if (strcmp(argv[1], "inet6") == 0)
		family = AF_INET6;
	else
		err_quit("invalid <address-family>", 1);
	doaliases = atoi(argv[2]);

	for (ifihead = ifi = get_ifi_info(family, doaliases);
		 ifi != NULL; ifi = ifi->ifi_next) {
		printf("%s: ", ifi->ifi_name);
		if (ifi->ifi_index != 0)
			printf("(%d) ", ifi->ifi_index);
		printf("<");
		if (ifi->ifi_flags & IFF_UP)			printf("UP ");
		if (ifi->ifi_flags & IFF_BROADCAST)		printf("BCAST ");
		if (ifi->ifi_flags & IFF_MULTICAST)		printf("MCAST ");
		if (ifi->ifi_flags & IFF_LOOPBACK)		printf("LOOP ");
		if (ifi->ifi_flags & IFF_POINTOPOINT)	printf("P2P ");
		printf("\b>\n");

		if ( (i = ifi->ifi_hlen) > 0) {
			ptr = ifi->ifi_haddr;
			do {
				printf("%s%x", (i == ifi->ifi_hlen) ? "  " : ":", *ptr++);
			} while (--i > 0);
			printf("\n");
		}
		if (ifi->ifi_mtu != 0)
			printf("  MTU: %d\n", ifi->ifi_mtu);

		char *tmp = NULL;
		size_t len = 0;
		struct sockaddr *sa;
		if ((sa = ifi->ifi_addr) != NULL)
			printf("  IP addr: %s\n", sa_ntop(sa, &tmp, &len));

		if ((sa = ifi->ifi_ntmaddr) != NULL)
			printf("  network mask: %s\n", sa_ntop(sa, &tmp, &len));

		if ((sa = ifi->ifi_brdaddr) != NULL)
			printf("  broadcast addr: %s\n",
			       sa_ntop(sa, &tmp, &len));
		if ((sa = ifi->ifi_dstaddr) != NULL)
			printf("  destination addr: %s\n",
			       sa_ntop(sa, &tmp, &len));
	}
	free_ifi_info(ifihead);
	return 0;
}
