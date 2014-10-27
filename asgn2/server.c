#include "utils.h"

int read_serv_conf(struct serv_conf * conf) {
    FILE * conf_file = fopen("server.in", "r");
    fscanf(conf_file, " %d", &(conf->port_num));
    fscanf(conf_file, " %d", &(conf->sli_win_sz));
    fclose(conf_file);
}

void print_serv_conf(const struct serv_conf * config) {
    printf("\n");
    printf("Server Configuration from server.in:\n");
    printf("Well-known port number is %d\n", config->port_num);
    printf("Sending sliding-window size is %d (in datagram units)\n", config->sli_win_sz);
    printf("\n");
}

int main(int argc, char * const *argv) {

    // server configuration
    struct serv_conf server_config;

    // for interfaces
    struct ifi_info *ifi, *ifihead;
    int ifi_hlen;
    int interface_count = 0;
    struct sock_info_aux s_info[MAX_INTERFACE_NUM];

    // temp var
    u_char * ptr;
    struct sockaddr_in * sock_address;

    // the two socket
    int listen_fd, conn_fd;

    // get configuration
    read_serv_conf(&server_config);
    print_serv_conf(&server_config);

    return 0;
}
