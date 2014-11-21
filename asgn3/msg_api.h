#ifndef MSG_API_H
#define MSG_API_H

#include <stdint.h>

struct send_msg_hdr {
    uint32_t dst_ip;
    uint16_t dst_port;
    uint16_t flag;
    uint16_t msg_len;
};

struct recv_msg_hdr {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t msg_len;
};

int msg_send(
        int /* socket fd for write */,
        char * /* 'canonical' IP for destination, in P format */,
        uint16_t /* dest port number */, 
        char * /* message to be sent */,
        int /* flag, set then force a route discovery to dest */
        );

int msg_recv(
        int /* socket fd for read */,
        char * /* message received */,
        char * /* 'canonical' IP of the source which sent the message, in P format */,
        uint16_t * /* source port numer */
        );

#endif
