#pragma once
#include <stdint.h>
#include <netinet/in.h>
struct hwaddr {
	int       sll_ifindex;	 /* Interface number */
	uint16_t  sll_hatype;	 /* Hardware type */
	uint8_t   sll_halen;		 /* Length of address */
	uint8_t   sll_addr[8];	 /* Physical layer address */
}__attribute__((packed));

int areq(struct sockaddr *addr, socklen_t len, struct hwaddr *rep);
