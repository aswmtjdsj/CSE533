/* Our own header for the programs that need interface configuration info.
   Include this file, instead of "unp.h". */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdio.h>
#include <stdlib.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/time.h>

#define IFI_NAME 16		/* same as IFNAMSIZ in <net/if.h> */
#define IFI_HADDR  8		/* allow for 64-bit EUI-64 in future */

#define CASSERT(expr, str) typedef struct {int: -!!(expr); } assert_failed_##str

#define DATAGRAM_SIZE 512

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

// for binding interfaces of server
#define MAX_INTERFACE_NUM 10
struct sock_info_aux {
    int sock_fd;
    struct sockaddr * ip_addr;
    struct sockaddr * net_mask;
    struct sockaddr * subn_addr;
};

#define FLAG_NON_LOCAL 0
#define FLAG_LOCAL 1
#define FLAG_LOOP_BACK 2

#define LOOP_BACK_ADDR "127.0.0.1"

struct ifi_info *get_ifi_info(int, int);
void free_ifi_info(struct ifi_info *);
const char *sa_ntop(struct sockaddr *, char **, size_t *);
int check_address(struct sock_info_aux *, struct sockaddr *); // see line 43-45 for returning value
int islocal_addr(struct sockaddr_in *, struct sockaddr_in *);
void get_nonloopback_addr(struct sockaddr_in *);

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
    uint32_t sli_win_sz;
};

// active child info
struct child_info {
    pid_t pid;
    uint32_t syn_init;
    struct child_info * next;
};

// RTT measured in ms
#define RTT_RXTMIN 1000
#define RTT_RXTMAX 3000
#define RTT_MAXNREXMT 12

#define RTT_RTOCALC(ptr) ((ptr)->rtt_srtt + (((ptr)->rtt_rttvar) << 2))
struct rtt_info {
    int rtt_rtt; /* most recent measured RTT, in milliseconds */
    int rtt_srtt; /* smoothed RTT estimator, in milliseconds */
    int rtt_rttvar; /* smoothed mean deviation, in milliseconds */ 
    int rtt_rto; /* current RTO to use, in milliseconds */
    int rtt_nrexmt; /* # times retransmitted: 0, 1, 2, ... */
    uint32_t rtt_base; /* # msec since 1/1/1970 at start */
};

void rtt_debug(struct rtt_info *);
void rtt_init(struct rtt_info *);
void rtt_newpack(struct rtt_info *);
int rtt_start(struct rtt_info *);
void rtt_stop(struct rtt_info *, uint32_t);
int rtt_timeout(struct rtt_info *);
uint32_t rtt_ts(struct rtt_info *);
void dump_ifi_info(int, int );

static int
rtt_minmax(int rto) {
    if (rto < RTT_RXTMIN)
        rto = RTT_RXTMIN;
    else if (rto > RTT_RXTMAX)
        rto = RTT_RXTMAX;
    return (rto);
}

// my own error handle func, with errno-string translation
void my_err_quit(const char *);

// sender sliding window
struct sliding_window {
    uint8_t data_buf[DATAGRAM_SIZE];
    int data_sz;
    uint32_t seq;
    struct rtt_info rtt;
    int ack_times;
};

// timer queue
struct timer_info {
    struct timer_info * next;
    struct timeval set_time;
    struct timeval delay;
};

#endif
