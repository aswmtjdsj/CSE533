#ifndef MSG_API_H
#define MSG_API_H

#include <stdint.h>

struct msg_hdr {
    uint32_t dest_ip;
    uint16_t dest_port;
    uint16_t msg_len;
};

int msg_send(
        int /* socket fd for write */,
        char * /* 'canonical' IP for destination, in P format */,
        int /* dest port number */, 
        char * /* message to be sent */,
        int /* flag, set then force a route discovery to dest */
        );

int msg_recv(
        int /* socket fd for read */,
        char * /* message received */,
        char * /* 'canonical' IP of the source which sent the message, in P format */,
        int * /* source port numer */
        );

#endif
