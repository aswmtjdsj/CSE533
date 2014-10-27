#include "common.h"

void read_serv_config( struct serv_config * config ) {
    FILE * f_serv = fopen("server.in", "r");
    fscanf( f_serv, " %d", &(config->port_num));
    fscanf( f_serv, " %d", &(config->sli_win_size));
    fclose( f_serv);
}

void print_serv_config( struct serv_config config) {
    printf("\n");
    printf("Server Configuration from server.in:\n");
    printf("Well-known port number is %d\n", config.port_num);
    printf("Sending sliding-window size is %d (in datagram units)\n", config.sli_win_size);
    printf("\n");
}

void print_ifi_flags( struct ifi_info * ifi) {
    printf("<");
    if (ifi->ifi_flags & IFF_UP)			printf("UP ");
    if (ifi->ifi_flags & IFF_BROADCAST)		printf("BCAST ");
    if (ifi->ifi_flags & IFF_MULTICAST)		printf("MCAST ");
    if (ifi->ifi_flags & IFF_LOOPBACK)		printf("LOOP ");
    if (ifi->ifi_flags & IFF_POINTOPOINT)	printf("P2P ");
    printf(">\n");
}

int main() {

    // get server configuration
    struct serv_config server_config;
    read_serv_config( &server_config);
    print_serv_config( server_config);

    // bind host interfaces
    struct ifi_info *ifi, *ifihead;
    int hlen_cnt;
    u_char * ptr;
    int interface_count = 0;
    struct socket_info s_info[MAX_INTERFACE];

    // temporarily store the IPv4 address
    struct sockaddr_in * sock_address;

    // store lenght of the address
    socklen_t client_len, child_len, server_len;

    // the two socket
    int listen_fd, conn_fd;

    // for socket option
    const int on = 1, value = 1;

    // for send and receive
    char send_buff[PAYLOAD_SIZE], recv_buff[PAYLOAD_SIZE];
    printf("Server IFI Info:\n");
    for( ifihead = ifi = Get_ifi_info_plus(AF_INET, 1); 
            ifi != NULL; 
            ifi = ifi->ifi_next) {

        // temp variables
        struct sockaddr *ip_addr, *net_mask, *sub_net, *br_addr, *dst_addr;

        printf("%s: ", ifi->ifi_name);

        if (ifi->ifi_index != 0)
            printf("(%d) ", ifi->ifi_index);

        print_ifi_flags(ifi);

        if ( (hlen_cnt = ifi->ifi_hlen) > 0) {
            ptr = ifi->ifi_haddr;
            do {
                printf("%s%x", (hlen_cnt == ifi->ifi_hlen) ? "  " : ":", *ptr++);
            } while (--hlen_cnt > 0);
            printf("\n");
        }

        if (ifi->ifi_mtu != 0)
            printf("  MTU: %d\n", ifi->ifi_mtu);

        if ( (ip_addr = ifi->ifi_addr) != NULL)
            printf("  IP address: %s\n",
                    Sock_ntop_host(ip_addr, sizeof(struct sockaddr)));

        // set addr
        s_info[interface_count].ip_addr = Malloc(sizeof(struct sockaddr));
        memcpy(s_info[interface_count].ip_addr, ip_addr, sizeof(struct sockaddr));

        // Socket Operation 
        listen_fd = s_info[interface_count].sock_fd = Socket(AF_INET, SOCK_DGRAM, 0);

        Setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        // set port
        sock_address = ((struct sockaddr_in *)s_info[interface_count].ip_addr);
        sock_address->sin_family = AF_INET;
        sock_address->sin_port = htons(server_config.port_num);

        // Bind Operation 
        Bind(listen_fd, (SA *)s_info[interface_count].ip_addr, sizeof(struct sockaddr));

        // get netmask
        if ( (net_mask = ifi->ifi_ntmaddr) != NULL)
            printf("  network mask: %s\n",
                    Sock_ntop_host(net_mask, sizeof(struct sockaddr)));

        // set netmask
        s_info[interface_count].net_mask = Malloc(sizeof(struct sockaddr));
        memcpy(s_info[interface_count].net_mask, net_mask, sizeof(struct sockaddr));

        // set subnet addr
        s_info[interface_count].sn_addr = Malloc(sizeof(struct sockaddr));
        memcpy(s_info[interface_count].sn_addr, net_mask, sizeof(struct sockaddr));
        sub_net = s_info[interface_count].sn_addr;

        // bit-wise and
        (((struct sockaddr_in *)sub_net)->sin_addr).s_addr
            = (((struct sockaddr_in *)s_info[interface_count].ip_addr)->sin_addr).s_addr &
            (((struct sockaddr_in *)s_info[interface_count].net_mask)->sin_addr).s_addr;


        if ( (br_addr = ifi->ifi_brdaddr) != NULL)
            printf("  broadcast address: %s\n",
                    Sock_ntop_host(br_addr, sizeof(struct sockaddr)));
        if ( (dst_addr = ifi->ifi_dstaddr) != NULL)
            printf("  destination address: %s\n",
                    Sock_ntop_host(dst_addr, sizeof(struct sockaddr)));
        printf("\n");
        interface_count++;
    }

    // free resource
    free_ifi_info_plus( ifihead);

    // print out the bound interfaces
    int inter_index;
    printf("Store all the interfaces in our own data structure>\n");
    for( inter_index = 0; inter_index < interface_count; inter_index++) {
        printf("Bound Interface #%d:\n", inter_index);
        printf("\t(DEBUG)sock_fd: %d\n", s_info[inter_index].sock_fd);
        printf("\tIP Address: %s\n", Sock_ntop_host( s_info[inter_index].ip_addr, sizeof(struct sockaddr)));
        printf("\tPort Number: %d\n", ((struct sockaddr_in *)s_info[inter_index].ip_addr)->sin_port);
        printf("\tNetwork Mask: %s\n", Sock_ntop_host( s_info[inter_index].net_mask, sizeof(struct sockaddr)));
        printf("\tSubnet Address: %s\n", Sock_ntop_host( s_info[inter_index].sn_addr, sizeof(struct sockaddr)));
        printf("\n");
    }

    // select to wait for incoming stuff
    fd_set r_set;
    int max_fdcnt=0;
    FD_ZERO(&r_set);

    // fork a child to handle incoming stuff
    pid_t child_pid;

    printf("Using select, Waiting for incoming datagram...\n");

    for( ; ; ) {
        max_fdcnt = 0;
        for( inter_index = 0; inter_index < interface_count; inter_index++) {
            FD_SET(s_info[inter_index].sock_fd,&r_set);
            max_fdcnt = max(s_info[inter_index].sock_fd,max_fdcnt)+1;
        }
        //printf("(DEBUG) max_fdcnt: %d\n", max_fdcnt);
        Select(max_fdcnt, &r_set, NULL, NULL, NULL);

        for( inter_index = 0; inter_index < interface_count; inter_index++) {
            if(FD_ISSET(s_info[inter_index].sock_fd, &r_set)) {
                //struct msghdr * msg;
                //recvmsg(s_info[inter_index].sock_fd, msg, 0);
                char recv_buff[PAYLOAD_SIZE];
                struct sockaddr client_address;
                client_len = sizeof(client_address); // initialization is necessary

                char file_name[PAYLOAD_SIZE];
                struct udp_hdr recv_hdr;
                Recvfrom(s_info[inter_index].sock_fd, recv_buff, PAYLOAD_SIZE, 0, &client_address, &client_len);

                memcpy( &recv_hdr, recv_buff, sizeof(struct udp_hdr));
                memcpy( file_name, recv_buff+sizeof(struct udp_hdr), PAYLOAD_SIZE - sizeof(struct udp_hdr));
                printf("Connected from Client IP Address: %s\n", Sock_ntop_host(&client_address, sizeof(struct sockaddr)));
                printf("\tWith Port Number: %d\n", ((struct sockaddr_in *)&client_address)->sin_port);
                printf("\tRequested filename by Client: %s\n", file_name);

                printf("\n");

                if( (child_pid = fork()) == 0) { // child
                    // connection socket
                    printf("Child Server forked!\n");

                    struct sockaddr child_address;
                    memcpy(&child_address, s_info[inter_index].ip_addr, sizeof(struct sockaddr));
                    int net_flag = 0, SEND_FLAG = 0;// 0 for non_local, 1 for local, 2 for loop_back
                    // detect local or not
                    if( strcmp(Sock_ntop_host( &client_address, sizeof(struct sockaddr)), "127.0.0.1") == 0) {
                        net_flag = 2;
                    }
                    else {
                        if( ((((struct sockaddr_in *)&child_address)->sin_addr).s_addr
                                    & (((struct sockaddr_in *)(s_info[inter_index].net_mask))->sin_addr).s_addr) 
                                == ((((struct sockaddr_in *)&client_address)->sin_addr).s_addr
                                    & (((struct sockaddr_in *)(s_info[inter_index].net_mask))->sin_addr).s_addr)) {
                            net_flag = 1;
                        }
                    }

                    // show the results of determination
                    if( net_flag == 0) {
                        printf("The Client is not local>\n");
                    }
                    else if( net_flag == 1) {
                        printf("The Client is local>\n");
                        SEND_FLAG = MSG_DONTROUTE;
                    }
                    else { 
                        printf("The Client and Server are the same host (loop_back)>\n");
                    }

                    printf("Server IP Address: %s\n", Sock_ntop_host(&child_address, sizeof(struct sockaddr)));
                    printf("Client IP Address: %s\n", Sock_ntop_host(&client_address, sizeof(struct sockaddr)));
                    printf("\n");

                    // Create UDP and Bind Connection Socket
                    conn_fd = Socket(AF_INET, SOCK_DGRAM, 0);
                    Setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
                    ((struct sockaddr_in *)&child_address)->sin_family = AF_INET;
                    ((struct sockaddr_in *)&child_address)->sin_port = htons(0); // wild_card

                    Bind(conn_fd, (SA *) &child_address, sizeof(struct sockaddr));

                    child_len = sizeof(struct sockaddr);
                    Getsockname(conn_fd, (SA *)&child_address, &child_len);
                    printf("After Binding the connection socket>\n");
                    printf("Server IP Address: %s\n", Sock_ntop_host(&child_address, sizeof(struct sockaddr)));
                    printf("\tPort Number: %d\n", ((struct sockaddr_in *)&child_address)->sin_port);
                    printf("\n");

                    // connect the client
                    Connect(conn_fd, (SA *) &client_address, sizeof(struct sockaddr));
                    memcpy(send_buff, &child_address, sizeof(struct sockaddr));
                    printf("Via listening socket, sending ACK info: %s\n", send_buff);
                    Sendto(s_info[inter_index].sock_fd, send_buff, PAYLOAD_SIZE, SEND_FLAG, &client_address, client_len);

                    // get ack from client
                    //Recvfrom(conn_fd, recv_buff, PAYLOAD_SIZE, 0, &client_address, &client_len);
                    Recvfrom(conn_fd, recv_buff, PAYLOAD_SIZE, 0, NULL, NULL);
                    printf("Receiving %s from Client via connection socket>\n", recv_buff);
                    if( strcmp(recv_buff, "ACK") == 0) {
                        Close(s_info[inter_index].sock_fd);
                        // send file
                        FILE * f_serv = fopen( file_name, "r");
                        printf("\n");
                        printf("Start sending file...\n");
                        int seq_num = 0;
                        while(Fgets(send_buff, PAYLOAD_SIZE, f_serv) != NULL) {
                            printf("%d %s", ++seq_num, send_buff);
                            Sendto(conn_fd, send_buff, strlen(send_buff), SEND_FLAG, NULL, client_len);
                        }
                        printf("\n");
                        snprintf( send_buff, PAYLOAD_SIZE, "FIN");
                        Sendto(conn_fd, send_buff, strlen(send_buff), SEND_FLAG, NULL, client_len);
                    }
                    printf("Complete Sending file! Waiting for another request!\n");
                    printf("\n");

                    exit(0);
                }
                else { // parent
                    break;
                }

            }
        }
    }

    // release socket_info
    for( inter_index = 0; inter_index < interface_count; inter_index++) {
        free( s_info[inter_index].ip_addr);
        free( s_info[inter_index].net_mask);
        free( s_info[inter_index].sn_addr);
    }

    return 0;
}
