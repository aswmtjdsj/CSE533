#include "const.h"

/*const char vm_name[][5] = { "vm1", "vm2", "vm3", "vm4", "vm5", "vm6", "vm7", "vm8", "vm9", "vm10"};

const char vm_ip[][16] = { "130.245.156.21", "130.245.156.22", "130.245.156.23", "130.245.156.24", "130.245.156.25", "130.245.156.26", "130.245.156.27", "130.245.156.28", "130.245.156.29", "130.245.156.20"};*/

int main() {

    log_info("This is a time server!\n");

    int sock_un_fd;
    struct sockaddr_un serv_addr, serv_addr_info;
    char local_host_name[HOST_NAME_MAX_LEN] = "";
    char time_serv_sun_path[SUN_PATH_MAX_LEN] = TIM_SERV_SUN_PATH;
    int path_len = 0;
    socklen_t sock_len = 0;

    /* for receiving message */
    struct in_addr src_addr;
    int ret = 1;
    char msg_recvd[MSG_MAX_LEN], src_ip[IP_P_MAX_LEN] = "0.0.0.0";//"130.245.156.22";
    uint16_t src_port;
    // char src_host_name[HOST_NAME_MAX_LEN] = "";
    struct hostent * src_host;

    // for time stamp
    time_t a_clock;
    struct tm * cur_time;
    char ret_tm[MSG_MAX_LEN];

    log_info("Server is going to create UNIX Domain socket!\n");
    // get local host
    if(gethostname(local_host_name, sizeof(local_host_name)) < 0) {
        my_err_quit("gethostname error");
    }

    log_info("Current node: <%s>\n", local_host_name);

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

    log_info("Waiting for incoming requests ...\n");
    // handle message
    for( ; ; ) {
        if(msg_recv(sock_un_fd, msg_recvd, src_ip, &src_port) < 0) {
            my_err_quit("msg_recv error");
        }

        // get host name by ip
        memset(&src_addr, 0, sizeof(src_addr));

        if((ret = inet_pton(AF_INET, src_ip, &src_addr)) < 0) {
            log_err("Input IP address [%s] to be converted to network byte order, is invalid!\n", src_ip);
            log_warn("Error Occurred! Go back to wait for incoming requests ...\n");
            continue;
        }
        if(ret == 0) {
            log_warn("[%s] does not contain a character string representing a valid network address in the specified address family", src_ip);
            continue;
        }

        if((src_host = gethostbyaddr(&src_addr, sizeof(struct in_addr), AF_INET)) == NULL) {
            switch(h_errno) {
                case HOST_NOT_FOUND:
                    log_err("Source host %s is unknown!\n", src_ip);
                    break;
                case NO_ADDRESS:
                    // case NO_DATA:
                    log_err("The requested name is valid but does not have an IP address\n");
                    break;
                case NO_RECOVERY:
                    log_err("A nonrecoverable name server error occurred!\n");
                    break;
                case TRY_AGAIN:
                    log_err("A temporary error occurred on an authoritative name server. Try again later.\n");
                    break;
            }

            log_warn("Error Occurred! Go back to wait for incoming requests ...\n");
            continue;
        }

        log_info("server at node <%s> responding to request from <%s>\n", local_host_name, src_host->h_name);

        time(&a_clock);
        cur_time = localtime(&a_clock);
        strcpy(ret_tm, asctime(cur_time));
        ret_tm[strlen(ret_tm)-1] = 0;
        log_debug("returning timestamp: %s\n", ret_tm);
        if(msg_send(sock_un_fd, src_ip, src_port, ret_tm, NON_REDISCOVER) < 0) {
            my_err_quit("msg_send error");
        }
    }

//EVERYTHING_DONE:
    // garbage collection
    log_info("All work done!\n");
    log_info("Cleaning resources ...\n");
    unlink(time_serv_sun_path);
    log_info("Quit now!\n");
    return 0;
}
