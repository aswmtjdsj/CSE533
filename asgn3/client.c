#include <stdio.h>
#include "utils.h"
#include "log.h"
#include "msg_api.h"
#include "mainloop.h"

int main(int argc, char * const *argv) {

    int sock_fd;
    // struct sockaddr_un cli_addr, serv_addr;

    info_print("This is a time client!\n");
    if((sock_fd = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0) {
        my_err_quit("socket error");
    }

    return 0;
}
