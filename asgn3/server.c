#include "const.h"

int main() {

    int sock_un_fd;
    struct sockaddr_un serv_addr, serv_addr_info;
    char local_host_name[HOST_NAME_MAX_LEN], req_host_name[HOST_NAME_MAX_LEN];
    char time_serv_sun_path[SUN_PATH_MAX_LEN] = TIM_SERV_SUN_PATH;
    int path_len = 0;
    socklen_t sock_len = 0;

    log_info("Server is going to create UNIX Domain socket!\n");
    // get local host
    if(gethostname(local_host_name, sizeof(local_host_name)) < 0) {
        my_err_quit("gethostname error");
    }

    log_info("Current node: %s\n", local_host_name);

    unlink(time_serv_sun_path);
    path_len = strlen(time_serv_sun_path);

    // create unix domain socket
    if((sock_un_fd = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0) {
        unlink(time_serv_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("socket error");
    }

    // init serv addr
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_LOCAL;
    strncpy(serv_addr.sun_path, time_serv_sun_path, path_len);
    serv_addr.sun_path[path_len] = 0;

    // bind
    if(bind(sock_un_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        unlink(time_serv_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("bind error");
    }

    // after binding, get sock info
    sock_len = sizeof(serv_addr_info);
    if(getsockname(sock_un_fd, (struct sockaddr *) &serv_addr_info, &sock_len) < 0) {
        unlink(time_serv_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("getsockname error");
    }

    log_debug("Server unix domain socket created, socket sun path: %s, socket structure size: %u\n", serv_addr_info.sun_path, (unsigned int) sock_len);

    // handle message

EVERYTHING_DONE:
    // garbage collection
    log_info("All work done!\n");
    log_info("Cleaning resources ...\n");
    unlink(time_serv_sun_path);
    log_info("Quit now!\n");
    return 0;
}
