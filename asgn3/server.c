#include "const.h"

int main() {

    int sock_un_fd;
    struct sockaddr_un serv_addr, serv_addr_info;
    char local_host_name[32], req_host_name[32];

    log_info("Server is going to create UNIX Domain socket!\n");
    // get local host
    if(gethostname(local_host_name, sizeof(local_host_name)) < 0) {
        my_err_quit("gethostname error");
    }
    log_info("Current node: %s\n", local_host_name);

    return 0;
}
