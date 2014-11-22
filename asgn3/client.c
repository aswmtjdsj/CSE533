#include <signal.h>
#include <setjmp.h>

#include "const.h"

static sigjmp_buf jmpbuf;
void sig_alarm(int signo) {
    siglongjmp(jmpbuf, 1);
}

int handle_input() {
    // input command
    char input_c[INPUT_BUF_MAX_LEN];
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
    socklen_t sock_len = 0;
    // client address
    struct sockaddr_un cli_addr, cli_addr_info;
    struct sockaddr_in dest_addr;
    // path template for mkstemp to use
    char cli_sun_path[SUN_PATH_MAX_LEN] = CLI_SUN_PATH;
    int path_len = 0;
    // local host name and dest name
    char local_host_name[HOST_NAME_MAX_LEN], tmp_str[HOST_NAME_MAX_LEN], dest_host_name[HOST_NAME_MAX_LEN], * dest_ip = NULL;
    // server vm id
    int dest_id = -1;
    struct hostent * dest_host;
    // size_t addr_len = 0;
    // message sending
    int send_flag;
    // message received
    char msg_recvd[MSG_MAX_LEN], src_ip[IP_P_MAX_LEN] = "0.0.0.0";
    uint16_t src_port;

    log_info("Client is going to create UNIX Domain socket!\n");

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
    strncpy(cli_addr.sun_path, cli_sun_path, path_len);
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

    log_debug("Client unix domain socket created, socket sun path: %s, socket structure size: %u\n", cli_addr_info.sun_path, (unsigned int) sock_len);

    // get local host
    if(gethostname(tmp_str, sizeof(tmp_str)) < 0) {
        unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("gethostname error");
    }
    strcpy(local_host_name, tmp_str);

    log_info("Current node: <%s>\n", local_host_name);

SELECT_LABLE:
    log_info("Select a server (destination) node (a numeric value [1-10] denoting vm[1-10], or \'Q\' to quit the program> ");
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
            log_info("<%s> selected!\n", dest_host_name);
            break;
    }

    // get dest host by name
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
        log_warn("Please go back to select another server/destination node.\n");
        goto SELECT_LABLE;
    }

    // get host ip by host_ent structure
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_addr = *((struct in_addr *)(dest_host->h_addr_list[0]));
    dest_ip = inet_ntoa(dest_addr.sin_addr); // network byte order to host byte order
    // log_debug("server (canonical) ip: %s\n", sa_ntop((struct sockaddr *) &dest_addr, &dest_ip, &addr_len));
    log_debug("server (canonical) ip: %s\n", dest_ip);

    log_info("Client at node <%s> is sending requests to server destination at <%s>\n", local_host_name, dest_host->h_name);

    // send and receive message
    send_flag = NON_REDISCOVER;

SEND_MESSAGE:
    if(msg_send(sock_un_fd, dest_ip, TIM_SERV_PORT, "Q", send_flag) < 0) {
        // my_err_quit("msg_send error");
        log_err("msg_send error\n");
        goto SELECT_LABLE;
    }

    // block in msg_recv

    // timeout using signal alarm
    signal(SIGALRM, sig_alarm);
    alarm(SEND_MAX_TIMEOUT);

    if(sigsetjmp(jmpbuf, 1) != 0) {
        // msg_recv timed out
        log_warn("Client at node <%s>: timeout on response from <%s>\n", local_host_name, dest_host->h_name);
        if(send_flag == NON_REDISCOVER) { // for the first timeout, force re-tran; otherwise, give up
            log_info("Gonna retransmit the reponse from <%s> to <%s>, with route-discovery flag set\n", local_host_name, dest_host->h_name);
            send_flag = EN_REDISCOVER;
            goto SEND_MESSAGE;
        } else {
            log_err("Retransmission has reached the limit, gonna give up!\n");
            // no need, there will be no more than one alarm
            // alarm(0); // give up re-sending
            goto SELECT_LABLE;
        }
    }

    if(msg_recv(sock_un_fd, msg_recvd, src_ip, &src_port) < 0) {
        log_err("msg_recv error\n");
        goto SELECT_LABLE;
    }
    alarm(0); // successfully received something

    log_info("Client at node <%s>: received from <%s> {%s}\n", local_host_name, dest_host->h_name, msg_recvd);

    // if all go on well and finished, then go back to promption
    log_info("Current work done! Go back to destination selection!\n");
    goto SELECT_LABLE;

EVERYTHING_DONE:
    // garbage collection
    log_info("All work done!\n");
    log_info("Cleaning resources ...\n");
    unlink(cli_sun_path); // we should manually collect junk, maybe marked as TODO
    log_info("Quit now!\n");
    return 0;
}
