#include "utils.h"
#include "log.h"

int read_serv_conf(struct serv_conf * conf) {
    FILE * conf_file = fopen("server.in", "r");
    fscanf(conf_file, " %d", &(conf->port_num));
    fscanf(conf_file, " %d", &(conf->sli_win_sz));
    fclose(conf_file);
}

void print_serv_conf(const struct serv_conf * config) {
    printf("Server Configuration from server.in:\n");
    printf("Well-known port number is %d\n", config->port_num);
    printf("Sending sliding-window size is %d (in datagram units)\n", config->sli_win_sz);
    printf("\n");
}

void print_ifi_flags(struct ifi_info * ifi) {
    printf("<");
    if (ifi->ifi_flags & IFF_UP)			printf("UP ");
    if (ifi->ifi_flags & IFF_BROADCAST)		printf("BCAST ");
    if (ifi->ifi_flags & IFF_MULTICAST)		printf("MCAST ");
    if (ifi->ifi_flags & IFF_LOOPBACK)		printf("LOOP ");
    if (ifi->ifi_flags & IFF_POINTOPOINT)	printf("P2P ");
    printf(">\n");
}

int main(int argc, char * const *argv) {

    // server configuration
    struct serv_conf config_serv;

    // for interfaces
    struct ifi_info *ifi, *ifihead;
    int ifi_hlen;
    int inter_index = 0;
    struct sock_info_aux sock_data_info[MAX_INTERFACE_NUM];

    // temp var
    u_char * yield_ptr = NULL;
    struct sockaddr_in * s_ad;
    struct sockaddr * ip_addr, * net_mask, * sub_net, * br_addr, * dst_addr;
    char * tmp_str = NULL;
    size_t addr_len = 0;
    int iter = 0;

    // only sub_net needed to be pre-allocated
    sub_net = malloc(sizeof(struct sockaddr));

    // err ret
    int err_ret = 0;

    // the two socket
    int listen_fd, conn_fd;
    int on_flag = 1;

    // get configuration
    printf("[CONFIG]\n");
    read_serv_conf(&config_serv);
    print_serv_conf(&config_serv);

    // IFI INFO
    printf("[IFI] Info of %s:\n", "Server");
    for( ifihead = ifi = get_ifi_info(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next) {
        // Interface
        printf("%s: ", ifi->ifi_name);

        if (ifi->ifi_index != 0)
            printf("(%d) ", ifi->ifi_index);

        print_ifi_flags(ifi);

        /*if ( (ifi_hlen = ifi->ifi_hlen) > 0) {
            ptr = ifi->ifi_haddr;
            do {
                printf("%s%x", (ifi_hlen == ifi->ifi_hlen) ? "  " : ":", *ptr++);
            } while (--ifi_hlen> 0);
            printf("\n");
        }*/

        if (ifi->ifi_mtu != 0) {
            printf("  MTU: %d\n", ifi->ifi_mtu);
        }

        if ((ip_addr = ifi->ifi_addr) != NULL) {
            printf("  IP address: %s\n", sa_ntop(ip_addr, &tmp_str, &addr_len));
        }

        // for interface array
        
        // set server address
        sock_data_info[inter_index].ip_addr = malloc(sizeof(struct sockaddr));
        memcpy((void *)sock_data_info[inter_index].ip_addr, (void *)ip_addr, sizeof(struct sockaddr));

        // set socket and option
        if((err_ret = (sock_data_info[inter_index].sock_fd = socket(AF_INET, SOCK_DGRAM, 0))) < 0) {
            err_quit("socket error: %e", err_ret);
        }

        if((err_ret = setsockopt(sock_data_info[inter_index].sock_fd, SOL_SOCKET, SO_REUSEADDR, &on_flag, sizeof(on_flag))) < 0) {
            err_quit("set socket option error: %e", err_ret);
        }

        // set server port
        ((struct sockaddr_in *)sock_data_info[inter_index].ip_addr)->sin_family = AF_INET;
        ((struct sockaddr_in *)sock_data_info[inter_index].ip_addr)->sin_port = htons(config_serv.port_num);

        // get interface netmask
        if ((net_mask = ifi->ifi_ntmaddr) != NULL) {
            printf("  network mask: %s\n", sa_ntop(net_mask, &tmp_str, &addr_len));
        }

        // set interface netmask
        sock_data_info[inter_index].net_mask = malloc(sizeof(struct sockaddr));
        memcpy(sock_data_info[inter_index].net_mask, net_mask, sizeof(struct sockaddr));

        // set interface subnet
        sock_data_info[inter_index].subn_addr = malloc(sizeof(struct sockaddr));
        // bit-wise and
        memcpy(sub_net, net_mask, sizeof(struct sockaddr));
        ((struct sockaddr_in *)sub_net)->sin_addr.s_addr = ((struct sockaddr_in *)ip_addr)->sin_addr.s_addr & ((struct sockaddr_in *)net_mask)->sin_addr.s_addr;
        memcpy(sock_data_info[inter_index].subn_addr, sub_net, sizeof(struct sockaddr));
        log_debug("  [DEBUG] sub net: %s\n", sa_ntop(sub_net, &tmp_str, &addr_len));

        // broadcast address
        if ((br_addr = ifi->ifi_brdaddr) != NULL) {
            printf("  broadcast address: %s\n", sa_ntop(br_addr, &tmp_str, &addr_len));
        }

        // destination address
        if ((dst_addr = ifi->ifi_dstaddr) != NULL) {
            printf("  destination address: %s\n", sa_ntop(dst_addr, &tmp_str, &addr_len));
        }

        printf("\n");
        inter_index++; 
    }
    free_ifi_info(ifihead);

    // info for interface array
    printf("Bound interfaces>\n");
    for(iter = 0; iter < inter_index; iter++) {
        printf("Interface #%d:\n", iter);
        log_info("\t(DEBUG) sock_fd: %d\n", sock_data_info[iter].sock_fd);
        printf("\tIP Address: %s\n", sa_ntop(sock_data_info[iter].ip_addr, &tmp_str, &addr_len));
        printf("\tPort Number: %d\n", ((struct sockaddr_in *)sock_data_info[iter].ip_addr)->sin_port);
        printf("\tNetwork Mask: %s\n", sa_ntop(sock_data_info[iter].net_mask, &tmp_str, &addr_len));
        printf("\tSubnet Address: %s\n", sa_ntop(sock_data_info[iter].subn_addr, &tmp_str, &addr_len));
        printf("\n");
    }

    // use select for incoming connection
    fd_set f_s;
    int max_fd_count = 0;
    FD_ZERO(&f_s);

    // fork a child to handle incoming conn
    pid_t child_pid;

    printf("Expecting upcoming datagram...\n");
    for( ; ; ) {
        max_fd_count = 0;
        for(iter = 0; iter < inter_index; iter++) {
            FD_SET(sock_data_info[iter].sock_fd, &f_s);
            max_fd_count= ((sock_data_info[iter].sock_fd > max_fd_count) ? sock_data_info[iter].sock_fd : max_fd_count) + 1;
        }

        if((err_ret = select(max_fd_count, &f_s, NULL, NULL, NULL)) < 0) {
            if(errno == EINTR) {
                /* how to handle */
                continue;
            }
        } else {
            err_quit("select error: %d", err_ret);
        }

        for(iter = 0; iter < inter_index; iter++) {
            if(FD_ISSET(sock_data_info[iter].sock_fd, &f_s)) {

            }
        }
    }

    // garbage collection
    printf("Cleaning junk...\n");
    if(ip_addr != NULL) {
        free(ip_addr);
    }

    if(net_mask != NULL) {
        free(net_mask);
    }

    if(sub_net != NULL) {
        free(sub_net);
    }

    for(iter = 0; iter < inter_index; iter++) {
        free(sock_data_info[iter].ip_addr);
        free(sock_data_info[iter].net_mask);
        free(sock_data_info[iter].subn_addr);
    }

    return 0;
}
