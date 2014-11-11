#include <stdio.h>
#include <sys/un.h>
#include <netdb.h>

#include "utils.h"
#include "log.h"
#include "msg_api.h"
#include "mainloop.h"

int handle_input() {
    // input command
    char input_c[20];
    int char_cnt = 0;
    int ret_value = 0;
    while(isalnum(input_c[char_cnt] = getchar())) {
        char_cnt++;
        if(char_cnt > 2) {
            fflush(stdin);
            return -1;
        }
    }
    fflush(stdin);
    input_c[char_cnt] = 0;

    if(char_cnt == 1 && input_c[0] == 'Q') {
        return 0;
    }

    int i = 0;
    for(i = 0; i < char_cnt; i++) {
        if(!isdigit(input_c[i])) {
            return -1;
        }
    }

    sscanf(input_c, "%d", &ret_value);
    if(ret_value <= 0 || ret_value > 10) {
        return -1;
    }

    return ret_value;
}

int main(int argc, char * const *argv) {

    info_print("This is a time client!\n");

    // unix domain socket descriptor
    int sock_un_fd;
    // socket structure length
    int sock_len;
    // client address
    struct sockaddr_un cli_addr, cli_addr_info, dest_addr;
    // path template for mkstemp to use
    char cli_sun_path[100] = "client_xiangyu_XXXXXX";
    int path_len = 0;
    // local host name and dest name
    char local_host_name[32], dest_host_name[32];
    // server vm id
    int dest_id = -1;
    struct hostent * dest_host;

    info_print("Client is goint to create UNIX Domain socket!\n");

    if(mkstemp(cli_sun_path) < 0) {
        unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("mkstemp error");
    }
    // before first use, we should unlink it
    unlink(cli_sun_path);
    path_len = strlen(cli_sun_path);
    
    log_debug("Client created a temporary sun path: %s\n", cli_sun_path);

    // create unix domain socket
    if((sock_un_fd = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0) {
        unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("socket error");
    }

    memset(&cli_addr, 0, sizeof(struct sockaddr_un));
    cli_addr.sun_family = AF_LOCAL;
    strncpy(cli_addr.sun_path, cli_sun_path, strlen(cli_sun_path));
    cli_addr.sun_path[path_len] = 0;

    if(bind(sock_un_fd, (struct sockaddr *) &cli_addr, sizeof(cli_addr)) < 0) {
        unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("bind error");
    }
    
    // after binding, get sock info
    sock_len = sizeof(cli_addr_info);
    if(getsockname(sock_un_fd, (struct sockaddr *) &cli_addr_info, &sock_len) < 0) {
        unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("getsockname error");
    }
    info_print("Client unix domain socket created, socket sun path: %s, socket structure size: %u\n", cli_addr_info.sun_path, (unsigned int) sock_len);
    
    // get local host
    if(gethostname(local_host_name, sizeof(local_host_name)) < 0) {
        unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("gethostname error");
    }
    info_print("Current node: %s\n", local_host_name);

SELECT_LABLE:
    info_print("Select a server node (a numeric value [1-10] denoting vm[1-10], or \'Q\' to quit the program> ");
    switch((dest_id = handle_input())) {
        case -1:
            err_print("Invalid Command!\n");
            goto SELECT_LABLE;
            break;
        case 0:
            warn_print("Quit command detected!\n");
            goto EVERYTHING_DONE;
            break;
        default:
            sprintf(dest_host_name, "vm%d", dest_id);
            info_print("%s selected!\n", dest_host_name);
            break;
    }

    if((dest_host = gethostbyname(dest_host_name)) == NULL) {
        switch(h_errno) {
            case HOST_NOT_FOUND:
                err_print("Destination host %s not found!\n", dest_host_name);
                break;
            case NO_ADDRESS:
            // case NO_DATA:
                err_print("Destination host %s is valid, but does not have an IP address!\n", dest_host_name);
                break;
            case NO_RECOVERY:
                err_print("A nonrecoverable name server error occurred.!\n");
                break;
            case TRY_AGAIN:
                err_print("A temporary error occurred on an authoritative name server. Try again later.\n");
                break;
        }
        goto EVERYTHING_DONE;
    }

EVERYTHING_DONE:
    // garbage collection
    warn_print("All work done!\n");
    warn_print("Cleaning resources!\n");
    unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
    return 0;
}
