/* Our own header for the programs that need interface configuration info.
   Include this file, instead of "unp.h". */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include "log.h"

#define CASSERT(expr, str) typedef struct {int: -!!(expr); } assert_failed_##str

#define IFI_NAME	16	/* same as IFNAMSIZ in <net/if.h> */
#define IFI_HADDR	8	/* allow for 64-bit EUI-64 in future */
#define IFI_ALIAS	1	/* ifi_addr is an alias */

struct ifi_info {
  char   ifi_name[IFI_NAME];		/* interface name, null-terminated */
  int    ifi_index;			/* interface index */
  int    ifi_mtu;			/* interface MTU */
  short  ifi_flags;			/* IFF_xxx constants from <net/if.h> */
  short  ifi_myflags;			/* our own IFI_xxx flags */
  struct sockaddr	*ifi_addr;	/* primary address */
  struct sockaddr	*ifi_brdaddr;	/* broadcast address */
  struct sockaddr	*ifi_dstaddr;	/* destination address */
  struct sockaddr	*ifi_ntmaddr;	/* netmask address */
  uint8_t		ifi_hwaddr[8];	/* hardware address */
  short			ifi_halen;	/* hardware address length*/
  struct ifi_info	*ifi_next;	/* next of these structures */
};

static inline void __attribute__((noreturn))
err_quit(const char *msg, int code){
	fputs(msg, stderr);
	exit(code);
}

static inline int iseolc(char in) {
	return in == '\n' || in == '\r';
}

static inline int iseols(const char *in){
	char last = in[strlen(in)-1];
	return iseolc(last);
}


static inline void chomp(char *inout){
	int pos = strlen(inout)-1;
	while(iseolc(inout[pos])) {
		inout[pos] = 0;
		pos--;
	}
}

static inline void dump_lladdr(struct sockaddr_ll *addr){
	log_info("\nDump ll address info:\n");
	log_info("\tProtocol number: %04X\n", ntohs(addr->sll_protocol));
	log_info("\tInterface index: %d\n", addr->sll_ifindex);
	log_info("\tAddress type: %04X\n", addr->sll_hatype);
	log_info("\tPacket type: %02X\n", addr->sll_pkttype);
	log_info("\tAddress length: %d\n", addr->sll_halen);
	log_info("\tAddress: ");
	int i;
	for(i=0;i<addr->sll_halen;i++)
		log_info("%02X ", addr->sll_addr[i]);
	log_info("\n");
}

const char *sa_ntop(struct sockaddr *, char **, size_t *);

#define ENABLE_COLOR

void info_print(const char *, ...);
void warn_print(const char *, ...);
void error_print(const char *, ...);

void my_err_quit(const char * /* error info */);
#endif
void free_ifi_info(struct ifi_info *ifihead, int deep);
struct ifi_info * get_ifi_info(int family, int doaliases);
