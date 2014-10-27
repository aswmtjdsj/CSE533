#ifndef COMMON_H
#define COMMON_H

#include "math.h"
#include "unpifiplus.h"
#include "unprtt.h"

struct cli_config {
    char ip_addr[20];
    int port_num;
    struct sockaddr * serv_addr;
    char file_name[20];
    int sli_win_size;
    int seed;
    float p;
    int miu;
};

struct serv_config {
    int port_num;
    int sli_win_size;
};

struct socket_info {
    int sock_fd;
    struct sockaddr * ip_addr;
    struct sockaddr * net_mask;
    struct sockaddr * sn_addr;
} ;

struct udp_hdr {
    int seq_number;
    int ack;
    int window_notify;
};

#define PAYLOAD_SIZE 512
#define MAX_INTERFACE 10

#endif
