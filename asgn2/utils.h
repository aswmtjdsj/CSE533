/* Our own header for the programs that need interface configuration info.
   Include this file, instead of "unp.h". */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdio.h>
#include <stdlib.h>
#include <net/if.h>
#include <string.h>

#define IFI_NAME 16		/* same as IFNAMSIZ in <net/if.h> */
#define IFI_HADDR  8		/* allow for 64-bit EUI-64 in future */

struct ifi_info {
  char   ifi_name[IFI_NAME];	/* interface name, null-terminated */
  int    ifi_index;		/* interface index */
  int    ifi_mtu;		/* interface MTU */
  short  ifi_flags;		/* IFF_xxx constants from <net/if.h> */
  short  ifi_myflags;		/* our own IFI_xxx flags */
  struct sockaddr  *ifi_addr;	/* primary address */
  struct sockaddr  *ifi_brdaddr;/* broadcast address */
  struct sockaddr  *ifi_dstaddr;/* destination address */
  struct sockaddr  *ifi_ntmaddr;/* netmask address */
  struct ifi_info  *ifi_next;	/* next of these structures */
};

#define	IFI_ALIAS	1	/* ifi_addr is an alias */

struct sock_info_aux {
    int sock_fd;
    struct sockaddr * ip_addr;
    struct sockaddr * net_mask;
    struct sockaddr * subn_addr;
};

struct ifi_info *get_ifi_info(int, int);
void free_ifi_info(struct ifi_info *);
const char *sa_ntop(struct sockaddr *, char **, size_t *);
int check_address(struct sock_info_aux  *, struct sock_info_aux *);

static void __attribute__((noreturn))
err_quit(const char *msg, int code){
	fputs(msg, stderr);
	exit(code);
}

static int iseolc(char in) {
	return in == '\n' || in == '\r';
}

static int iseols(const char *in){
	char last = in[strlen(in)-1];
	return iseolc(last);
}


static void chomp(char *inout){
	int pos = strlen(inout)-1;
	while(iseolc(inout[pos])) {
		inout[pos] = 0;
		pos--;
	}
}

struct serv_conf {
    int port_num;
    int sli_win_sz;
};

#define MAX_INTERFACE_NUM 10

struct udp_hdr {
};

#define FLAG_NON_LOCAL 0
#define FLAG_LOCAL 1
#define FLAG_LOOP_BACK 2

#define LOOP_BACK_ADDR "127.0.0.1"

#endif
