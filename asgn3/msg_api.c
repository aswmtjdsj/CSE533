#include <stdio.h>
#include <stdlib.h>

#include "msg_api.h"
#include "const.h"

int msg_send(int sockfd, char * dst_ip, int dst_port, char * msg, int flag) {
    // TODO
    log_debug("Sending message\n");
    struct msg_hdr send_hdr;
    return 0;
}

int msg_recv(int sockfd, char * msg, char * src_ip, int * src_port) {
    // clear the heap mem
    if(msg != NULL) {
        free(msg);
        msg = NULL;
    }

    // TODO 
    struct msg_hdr send_hdr;

    return 0;
}
