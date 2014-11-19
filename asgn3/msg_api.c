#include <stdio.h>
#include <stdlib.h>

#include "msg_api.h"

int msg_send(int sockfd, char * dst_ip, int dst_port, char * msg, int flag) {
    // TODO
    return 0;
}

int msg_recv(int sockfd, char * msg, char * src_ip, int * src_port) {
    // clear the heap mem
    if(msg != NULL) {
        free(msg);
        msg = NULL;
    }
    if(src_ip != NULL) {
        free(src_ip);
        src_ip = NULL;
    }
    // TODO 

    return 0;
}
