#include "common.h"

void read_cli_config( struct cli_config * config ) {
    FILE * f_cli = fopen("client.in", "r");
    fscanf( f_cli, " %s", config->ip_addr);
    fscanf( f_cli, " %d", &(config->port_num));
    fscanf( f_cli, " %s", config->file_name);
    fscanf( f_cli, " %d", &(config->sli_win_size));
    fscanf( f_cli, " %d", &(config->seed));
    fscanf( f_cli, " %f", &(config->p));
    fscanf( f_cli, " %d", &(config->miu));
    fclose( f_cli);

    config->serv_addr = Malloc(sizeof(struct sockaddr));
    Inet_pton(AF_INET, config->ip_addr,
            &(((struct sockaddr_in *)(config->serv_addr))->sin_addr));
    ((struct sockaddr_in *)config->serv_addr)->sin_family = AF_INET;
    ((struct sockaddr_in *)config->serv_addr)->sin_port = htons(config->port_num);
}

void print_cli_config( struct cli_config config) {
    printf("\n");
    printf("Client Configuration from client.in:\n");
    printf("IP address of server is %s\n", Sock_ntop_host( config.serv_addr, sizeof(struct sockaddr *)));
    //printf("IP address of server is %s\n", config.ip_addr);
    printf("Well-known port number is %d\n", ((struct sockaddr_in *)config.serv_addr)->sin_port);
    printf("Name of file to be transferred is %s\n", config.file_name);
    printf("Receiving sliding-window size is %d (in datagram units)\n", config.sli_win_size);
    printf("Random generator seed value is %d\n", config.seed);
    printf("Probability of data loss is %.3f\n", config.p);
    printf("Mean of controlling rate is %d milliseconds\n", config.miu);
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

void rand_init( int seed) {
    srand(seed);
}

float rand_range() {
    float rate = (rand() % 20000) / 19999.;
    return rate;
}

float rand_range_plus() {
    float rate = (rand() % 20000 + 1) / 20001.;
    return rate;
}

int sent_success( float p) {
    float rate = rand_range();
    if( rate > p) {
        return 0;
    }
    else {
        return 1;
    }
}

float gen_var( int miu, float p) {
    return -1 * miu * log((double) rand_range_plus()); // in (0, 1) not [0, 1]
}

static struct rtt_info rttinfo;
static int rttinit = 0;
static struct msghdr msg_send, msg_recv;
static struct hdr {
    uint32_t seq; /* sequence # */
    uint32_t ts; /* timestamp when sent */
} sendhdr, recvhdr;

int main() {

    struct cli_config client_config;
    read_cli_config( &client_config);
    print_cli_config( client_config);

    // set client IP addr info
    struct ifi_info *ifi, *ifihead, *ifi_chosen;
    int value;
    u_char * ptr;
    struct sockaddr * client_addr = NULL;
    struct sockaddr *ip_addr, *net_mask, *br_addr, *dst_addr;
    int net_flag = 0, SEND_FLAG = 0; // 0 means not local, 1 means local, 2 means loopback 

    // show IFI info
    printf("Client IFI Info:\n");
    for( ifihead = ifi = Get_ifi_info_plus(AF_INET, 1); 
            ifi != NULL; 
            ifi = ifi->ifi_next) {
        printf("%s: ", ifi->ifi_name);

        if (ifi->ifi_index != 0)
            printf("(%d) ", ifi->ifi_index);

        print_ifi_flags(ifi);

        if ( (value = ifi->ifi_hlen) > 0) {
            ptr = ifi->ifi_haddr;
            do {
                printf("%s%x", (value == ifi->ifi_hlen) ? "  " : ":", *ptr++);
            } while (--value > 0);
            printf("\n");
        }

        if (ifi->ifi_mtu != 0)
            printf("  MTU: %d\n", ifi->ifi_mtu);


        if ( (ip_addr = ifi->ifi_addr) != NULL)
            printf("  IP address: %s\n",
                    Sock_ntop_host(ip_addr, sizeof(*ip_addr)));

        if ( (net_mask = ifi->ifi_ntmaddr) != NULL)
            printf("  network mask: %s\n",
                    Sock_ntop_host(net_mask, sizeof(*net_mask)));

        if ( (br_addr = ifi->ifi_brdaddr) != NULL)
            printf("  broadcast address: %s\n",
                    Sock_ntop_host(br_addr, sizeof(*br_addr)));
        if ( (dst_addr = ifi->ifi_dstaddr) != NULL)
            printf("  destination address: %s\n",
                    Sock_ntop_host(dst_addr, sizeof(*dst_addr)));
        printf("\n");
    }

    // set client ip address by comparing the server address with client according to ethernet

    // according to priority
    // determine the same host
    for( ifihead = ifi = Get_ifi_info_plus(AF_INET, 1); 
            ifi != NULL; 
            ifi = ifi->ifi_next) {

        if( client_addr != NULL) {
            break;
        }

        ip_addr = ifi->ifi_addr;
        net_mask = ifi->ifi_ntmaddr;

        if((((struct sockaddr_in *)ip_addr)->sin_addr).s_addr  == (((struct sockaddr_in *)client_config.serv_addr)->sin_addr).s_addr) {
            client_addr = Malloc(sizeof(struct sockaddr));
            memcpy( client_addr, ip_addr, sizeof(struct sockaddr));
            char loop_back[25] = "127.0.0.1";
            Inet_pton(AF_INET, loop_back, &(((struct sockaddr_in *)client_config.serv_addr)->sin_addr));
            Inet_pton(AF_INET, loop_back, &(((struct sockaddr_in *)client_addr)->sin_addr));
            net_flag = 2;
        }
    }

    // we must ensure the net_mask are the same
    // so we choose the highest bit-wise result
    // determine local
    uint32_t bit_result = 0;
    for( ifihead = ifi = Get_ifi_info_plus(AF_INET, 1); 
            ifi != NULL; 
            ifi = ifi->ifi_next) {

        ip_addr = ifi->ifi_addr;
        net_mask = ifi->ifi_ntmaddr;

        if( client_addr != NULL) {
            break;
        }

        if( client_addr == NULL && 
                ((((struct sockaddr_in *)client_config.serv_addr)->sin_addr).s_addr 
                 & (((struct sockaddr_in *)net_mask)->sin_addr).s_addr) 
                == 
                ((((struct sockaddr_in *)ip_addr)->sin_addr).s_addr 
                 & (((struct sockaddr_in *)net_mask)->sin_addr).s_addr)) {

            bit_result = max(((((struct sockaddr_in *)client_config.serv_addr)->sin_addr).s_addr & (((struct sockaddr_in *)net_mask)->sin_addr).s_addr), bit_result);

            ifi_chosen = ifi;
            net_flag = 1;
            SEND_FLAG = MSG_DONTROUTE;
        }

    }

    if( bit_result > 0) {
        client_addr = Malloc(sizeof(struct sockaddr));
        memcpy( client_addr, ifi_chosen->ifi_addr, sizeof(struct sockaddr));
    }

    // not local, randomly set
    for( ifihead = ifi = Get_ifi_info_plus(AF_INET, 1); 
            ifi != NULL; 
            ifi = ifi->ifi_next) {

        if( client_addr != NULL) {
            break;
        }

        // if not local
        if( ifi->ifi_next == NULL && client_addr == NULL) {
            client_addr = Malloc(sizeof(struct sockaddr));
            memcpy( client_addr, ip_addr, sizeof(struct sockaddr));
        }
    }

    free_ifi_info_plus( ifihead);

    if( net_flag == 0) {
        printf("The Server is not local>\n");
    }
    else if( net_flag == 1) {
        printf("The Server is local>\n");
    }
    else { 
        printf("The Server and Client are the same host (loop_back)>\n");
    }
    printf("Client IP Address selected: %s\n", Sock_ntop_host(client_addr, sizeof(struct sockaddr)));
    printf("Server IP Address selected: %s\n", Sock_ntop_host(client_config.serv_addr, sizeof(struct sockaddr)));
    printf("\n");

    // create UDP socket
    int sock_fd = Socket(AF_INET, SOCK_DGRAM, 0), client_len;
    ((struct sockaddr_in *)client_addr)->sin_family = AF_INET;
    ((struct sockaddr_in *)client_addr)->sin_port = htons(0);

    // set the no-route option
    const int on=1;
    Setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    // get bound socket
    Bind(sock_fd, (SA *) client_addr, sizeof(struct sockaddr));
    client_len = sizeof(struct sockaddr); // initialization is rather important!
    Getsockname(sock_fd, (SA *)client_addr, &client_len);
    printf("After Binding the socket of Client>\n");
    printf("Client Bound IP Address: %s\n", Sock_ntop_host(client_addr, sizeof(struct sockaddr *)));
    printf("\tPort Number: %d\n", ((struct sockaddr_in *)client_addr)->sin_port);

    printf("\n");

    // get server socket
    printf("After Connecting to the Server>\n");
    Connect(sock_fd, (SA *) client_config.serv_addr, sizeof(struct sockaddr));
    int serv_len = sizeof(struct sockaddr);
    Getpeername(sock_fd, (SA *) client_config.serv_addr, &serv_len);
    printf("Server IP Address: %s\n", Sock_ntop_host(client_config.serv_addr, sizeof(struct sockaddr)));
    printf("\tWell-known Port Number: %d\n", ((struct sockaddr_in *)client_config.serv_addr)->sin_port);

    char recv_buff[PAYLOAD_SIZE], send_buff[PAYLOAD_SIZE];
    // send the filename to server
    printf("Sent filename %s to Server.\n", client_config.file_name);
    //Sendmsg(sock_fd, &msg_send, 0);
    int seq_number = 1;// initial
    int ack = 0;
    int window_no = 0;// temporarily no use
    struct udp_hdr send_hdr;
    send_hdr.seq_number = seq_number;
    send_hdr.ack = ack;
    send_hdr.window_notify = window_no;

    memcpy(send_buff, &send_hdr, sizeof(struct udp_hdr));
    memcpy(send_buff+sizeof(struct udp_hdr), client_config.file_name, strlen(client_config.file_name));
    send_buff[sizeof(struct udp_hdr)+strlen(client_config.file_name)]=0;
    
    Sendto(sock_fd, send_buff, PAYLOAD_SIZE, SEND_FLAG, NULL, serv_len);

    printf("\n");
    printf("ACKed by Server>\n");
    int recv_len = Recvfrom(sock_fd, recv_buff, PAYLOAD_SIZE, 0, NULL, NULL);
    char ip_buff[PAYLOAD_SIZE];
    int final_port;
    struct sockaddr new_address;
    memcpy(&new_address, recv_buff, sizeof(struct sockaddr));
    memcpy(client_config.serv_addr, &new_address, sizeof(struct sockaddr));
    printf("Server specific IP: %s\n", Sock_ntop_host(client_config.serv_addr, sizeof(struct sockaddr)));
    printf("\tconnection port: %d\n", ((struct sockaddr_in *)(client_config.serv_addr))->sin_port);

    printf("\n");
    printf("Reconnect Server via new port\n");
    Connect(sock_fd, (SA *) client_config.serv_addr, sizeof(struct sockaddr));
    snprintf(send_buff, PAYLOAD_SIZE, "ACK");
    Sendto(sock_fd, send_buff, PAYLOAD_SIZE, SEND_FLAG, NULL, serv_len);
    printf("Start receiving file...\n");
    printf("\n");
    while((recv_len = Recvfrom(sock_fd, recv_buff, PAYLOAD_SIZE, 0, NULL, NULL)) >= 0) {
        recv_buff[recv_len] = 0;
        if( strcmp(recv_buff,"FIN") == 0) {
            break;
        }
        Fputs( recv_buff, stdout);
    }

    return 0;
}
