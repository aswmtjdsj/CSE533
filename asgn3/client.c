#include <stdio.h>
#include <sys/un.h>
#include <netdb.h>
#include <ctype.h>

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

    log_info("This is a time client!\n");

    // unix domain socket descriptor
    int sock_un_fd;
    // socket structure length
    socklen_t sock_len;
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

    log_info("Client is goint to create UNIX Domain socket!\n");

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
    log_info("Client unix domain socket created, socket sun path: %s, socket structure size: %u\n", cli_addr_info.sun_path, (unsigned int) sock_len);

    // get local host
    if(gethostname(local_host_name, sizeof(local_host_name)) < 0) {
        unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("gethostname error");
    }
    log_info("Current node: %s\n", local_host_name);

    int cmd_fd;
    if (argc>=3) {
        //Was supplied with a command server to connect to
        struct sockaddr_in cmdaddr;
        int ret = inet_pton(AF_INET, argv[1], &cmdaddr.sin_addr);
        int port = atoi(argv[2]);
        cmdaddr.sin_port = htons(port);
        cmdaddr.sin_family = AF_INET;
        if (ret) {
        cmd_fd = socket(AF_INET, SOCK_STREAM, 0);
        ret = connect(cmd_fd,(struct sockaddr *)&cmdaddr,sizeof(cmdaddr));
        if (ret != 0)
            log_info("Failed to connect to cmd server: "
                    "%s\n", strerror(errno));
        } else {
            write(cmd_fd, local_host_name, strlen(local_host_name));
            fflush(stdin);
            dup2(cmd_fd, fileno(stdin));
        }
    }

SELECT_LABLE:
    log_info("Select a server node (a numeric value [1-10] denoting vm[1-10], or \'Q\' to quit the program> ");
    switch((dest_id = handle_input())) {
        case -1:
            log_err("Invalid Command!\n");
            goto SELECT_LABLE;
            break;
        case 0:
            log_warn("Quit command detected!\n");
            goto EVERYTHING_DONE;
            break;
        default:
            sprintf(dest_host_name, "vm%d", dest_id);
            log_info("%s selected!\n", dest_host_name);
            break;
    }

    if((dest_host = gethostbyname(dest_host_name)) == NULL) {
        switch(h_errno) {
            case HOST_NOT_FOUND:
                log_err("Destination host %s not found!\n", dest_host_name);
                break;
            case NO_ADDRESS:
            // case NO_DATA:
                log_err("Destination host %s is valid, but does not have an IP address!\n", dest_host_name);
                break;
            case NO_RECOVERY:
                log_err("A nonrecoverable name server error occurred.!\n");
                break;
            case TRY_AGAIN:
                log_err("A temporary error occurred on an authoritative name server. Try again later.\n");
                break;
        }
        goto EVERYTHING_DONE;
    }

EVERYTHING_DONE:
    // garbage collection
    log_info("All work done!\n");
    log_info("Cleaning resources!\n");
    unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
    return 0;
}
