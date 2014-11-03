#include "utils.h"
#include "log.h"
#include "protocol.h"
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <setjmp.h>

int read_serv_conf(struct serv_conf * conf) {
    FILE * conf_file = fopen("server.in", "r");
    if(conf_file == NULL) {
        my_err_quit("No such file \"server.in\"!\n");
    }
    fscanf(conf_file, " %d", &(conf->port_num));
    fscanf(conf_file, " %u", &(conf->sli_win_sz));
    fclose(conf_file);

    return 0;
}

void print_serv_conf(const struct serv_conf * config) {
    printf("\n[INFO] Server Configuration from server.in:\n");
    printf("\t{\"Well-known port number\": %d, \"Sender sliding-window size(in datagram units)\": %d}\n", config->port_num, config->sli_win_sz);
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

void print_hdr(struct tcp_header * hdr) {
    printf("\t[DEBUG] tcp header <");
    if(hdr->flags & HDR_ACK) {
        printf(" ACK");
    }
    if(hdr->flags & HDR_SYN) {
        printf(" SYN");
    }
    if(hdr->flags & HDR_FIN) {
        printf(" FIN");
    }
    printf(" >\n");
    printf("\t\t{\"seq#\": %u, \"datagram ack#\": %u, \"sender timestamp\": %u, \"receiver timestamp\": %u, \"window size\": %u} \n", \
            hdr->seq, hdr->ack, hdr->tsopt, hdr->tsecr, hdr->window_size);
}

/*
 * for the retransmission during 3 step handshake
 * */
static sigjmp_buf jmpbuf;
const int no_rtt_max_time_out = 12; // seconds
const int no_rtt_init_time_out = 3; // seconds
int no_rtt_time_out;

void sig_alarm(int signo) {
    // non local jump, when alarm triggered
    siglongjmp(jmpbuf, 1);
}

void set_no_rtt_time_out() {
    // set the initial time-out value
    no_rtt_time_out = no_rtt_init_time_out;
}

/* 
 * handle zombie child proc
 * */
struct child_info * child_info_list, * cur_pt;
void sig_child(int signo) {
    pid_t pid;
    int stat;

    pid = wait(&stat);

    // remove exited child from active list
    struct child_info * temp_pt = child_info_list, * prev_pt = temp_pt;
    for(; temp_pt != NULL; ) {
        if(temp_pt->pid == pid) {
            if(prev_pt == temp_pt) { // in the front of the list
                child_info_list = temp_pt->next;
            } else {
                prev_pt->next = temp_pt->next;
            }
            free(temp_pt);
            break;
        }
        if(prev_pt != temp_pt) {
            prev_pt = temp_pt;
        }
        temp_pt = temp_pt->next;
    }

    printf("[INFO][parent] child process %u terminated\n", (uint32_t)pid);
}

/*
 * make tcp header
 * */
struct tcp_header * make_hdr(struct tcp_header * hdr, uint32_t seq, uint32_t ack, 
        uint32_t timestamp_sender, uint32_t timestamp_receiver,
        uint16_t flags, uint16_t window_sz) {
    hdr->seq = seq;
    hdr->ack = ack;
    hdr->tsopt = timestamp_sender;
    hdr->tsecr = timestamp_receiver;
    hdr->flags = flags;
    hdr->window_size = window_sz;

    printf("\n\t[INFO] Datagram to be sent!\n");
    print_hdr(hdr);

    hdr->seq = htonl(seq);
    hdr->ack = htonl(ack);
    hdr->tsopt = htonl(hdr->tsopt);
    hdr->tsecr = htonl(hdr->tsecr);
    hdr->flags = htons(flags);
    hdr->window_size = htons(window_sz);
    return hdr;
}

/*
 * make data gram
 * */
void make_dgram(uint8_t * dgram, struct tcp_header * hdr, void * payload, int payload_size, int * send_size) {
    memcpy(dgram, hdr, sizeof(struct tcp_header));
    memcpy(dgram + sizeof(struct tcp_header), payload, payload_size);
    *send_size = sizeof(struct tcp_header) + payload_size;

    printf("\t[INFO] Sending datagram size: %d\n", *send_size);
    printf("\t[INFO] Sending datagram data: (not shown)\n");//, (char *)payload);
}

/*
 * parse datagram
 * */
void parse_dgram(uint8_t * dgram, struct tcp_header * hdr, void * payload, int recv_size) {
    memcpy(hdr, dgram, sizeof(struct tcp_header));

    if(payload != NULL) {
        memcpy(payload, dgram + sizeof(struct tcp_header), recv_size - sizeof(struct tcp_header));
    }

    hdr->ack = ntohl(hdr->ack); // network presentation to host
    hdr->seq = ntohl(hdr->seq);
    hdr->tsopt = ntohl(hdr->tsopt);
    hdr->tsecr = ntohl(hdr->tsecr);
    hdr->flags = ntohs(hdr->flags);
    hdr->window_size = ntohs(hdr->window_size);
    
    printf("\n\t[INFO] Dgram received!\n");
    print_hdr(hdr);
    printf("\t[INFO] Received datagram size: %d\n", recv_size);
}

/*
 * sliding window mechanism
 */
struct sliding_window * sli_win;
uint32_t sli_win_sz, window_start, window_end, sent_not_ack, adv_win_sz, avail_win_sz;

/*
 * slow start: congestion window
 */
// #define SSTHRESH 64
int cwnd;

void build_window(uint32_t size) {
    // sender available empty window size
    // receiver advertisez window size
    // current in use window size
    avail_win_sz = adv_win_sz = sli_win_sz = size;
    window_start = sent_not_ack = window_end = 0; 
    // [a1 a2 a3 | a4 a5 a6]
    // [: window_start, sent and acked
    // |: sent but not acked
    // ]: window_end, can send
    sli_win = malloc(size * sizeof(struct sliding_window));
}

void destroy_window() {
    if(sli_win != NULL) {
        free(sli_win);
    }
}

/*
 * for RTT timer queue
 */
struct timer_info * timer_queue_front, * timer_queue_tail;

void timer_queue_init() {
    timer_queue_front = timer_queue_tail = NULL;
}

void timer_queue_push(struct timeval tv) {
    struct timer_info * temp = malloc(sizeof(struct timer_info));
    temp->next = NULL;
    temp->delay = tv;

	if(gettimeofday(&temp->set_time, NULL) < 0) {
	    my_err_quit("gettimeofday error");
	}

    if(timer_queue_front != NULL) {
        timer_queue_tail->next = temp;
        timer_queue_tail = timer_queue_tail->next;
    }
    else {
        timer_queue_front = temp;
        timer_queue_tail = timer_queue_front;
    }
    temp = timer_queue_front;
    /*for(; temp != NULL; temp = temp->next) {
        printf("[DEBUG] timer set: %d s, %d us; delay: %d s, %d us\n", 
                (int)temp->set_time.tv_sec,
                (int)temp->set_time.tv_usec,
                (int)temp->delay.tv_sec,
                (int)temp->delay.tv_usec);
    }*/
}

struct timer_info * timer_queue_pop() {

    struct timer_info * temp = timer_queue_front;

    timer_queue_front = timer_queue_front->next;

    return temp;
}

int main(int argc, char * const *argv) {

    // server configuration
    struct serv_conf config_serv;

    // for interfaces
    struct ifi_info *ifi, *ifihead;
    // int ifi_hlen;
    int inter_index = 0;
    struct sock_info_aux sock_data_info[MAX_INTERFACE_NUM];

    // temp var
    // u_char * yield_ptr = NULL;
    // struct sockaddr_in * s_ad;
    struct sockaddr * ip_addr, * net_mask, * sub_net, * br_addr, * dst_addr, 
                    * cli_addr = malloc(sizeof(struct sockaddr)),
                    * chi_addr = malloc(sizeof(struct sockaddr)); // cauz that
    // the last param for recvfrom func, should be initialized, which is a value-reference param
    char * tmp_str = NULL;
    size_t addr_len = 0, cli_len = sizeof(struct sockaddr), chi_len = sizeof(struct sockaddr);
    int iter = 0;

    // only sub_net needed to be pre-allocated
    sub_net = malloc(sizeof(struct sockaddr));

    // the two socket
    int conn_fd;
    int on_flag = 1;

    // recv and send buffer
    uint8_t recv_dgram[DATAGRAM_SIZE], send_dgram[DATAGRAM_SIZE];
    struct tcp_header recv_hdr, send_hdr;
    int recv_size = 0, sent_size = 0;

    // active child info linked list, maintained by parent
    cur_pt = child_info_list = NULL;

    // sliding window
    sli_win = NULL;

    // congestion window
    cwnd = 1;

    // for RTT timer
    timer_queue_init();

    // get configuration
    read_serv_conf(&config_serv);
    print_serv_conf(&config_serv);
    build_window(config_serv.sli_win_sz);

    // IFI INFO
    printf("\n[INFO] IFI Info of %s:\n", "Server");
    for( ifihead = ifi = get_ifi_info(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next) {
        // Interface
        printf("%s: ", ifi->ifi_name);
        if (ifi->ifi_index != 0) printf("(%d) ", ifi->ifi_index);
        print_ifi_flags(ifi);
        if (ifi->ifi_mtu != 0) printf("\tMTU: %d\n", ifi->ifi_mtu);
        if ((ip_addr = ifi->ifi_addr) != NULL) printf("\tIP address: %s\n", sa_ntop(ip_addr, &tmp_str, &addr_len)); 

        /* for interface array */
        // set server address
        sock_data_info[inter_index].ip_addr = malloc(sizeof(struct sockaddr));
        memcpy((void *)sock_data_info[inter_index].ip_addr, (void *)ip_addr, sizeof(struct sockaddr));

        // set socket and option
        if((sock_data_info[inter_index].sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            my_err_quit("socket error");
        }

        if(setsockopt(sock_data_info[inter_index].sock_fd, SOL_SOCKET, SO_REUSEADDR, &on_flag, sizeof(on_flag)) < 0) {
            my_err_quit("set socket option error");
        }

        // set server port
        ((struct sockaddr_in *)sock_data_info[inter_index].ip_addr)->sin_family = AF_INET;
        ((struct sockaddr_in *)sock_data_info[inter_index].ip_addr)->sin_port = htons(config_serv.port_num);
        // log_debug("[DEBUG] port num: %x\n", htons(config_serv.port_num));
        
        // bind sock to addr
        if(bind(sock_data_info[inter_index].sock_fd, (struct sockaddr *) sock_data_info[inter_index].ip_addr, sizeof(struct sockaddr)) < 0) {
            my_err_quit("bind error");
        }

        // get interface netmask
        if ((net_mask = ifi->ifi_ntmaddr) != NULL) printf("\tnetwork mask: %s\n", sa_ntop(net_mask, &tmp_str, &addr_len)); 

        // set interface netmask
        sock_data_info[inter_index].net_mask = malloc(sizeof(struct sockaddr));
        memcpy(sock_data_info[inter_index].net_mask, net_mask, sizeof(struct sockaddr));

        // set interface subnet
        sock_data_info[inter_index].subn_addr = malloc(sizeof(struct sockaddr));

        // bit-wise and
        memcpy(sub_net, net_mask, sizeof(struct sockaddr));
        ((struct sockaddr_in *)sub_net)->sin_addr.s_addr = ((struct sockaddr_in *)ip_addr)->sin_addr.s_addr & ((struct sockaddr_in *)net_mask)->sin_addr.s_addr;
        memcpy(sock_data_info[inter_index].subn_addr, sub_net, sizeof(struct sockaddr));
        log_debug("\t[DEBUG] sub net: %s\n", sa_ntop(sub_net, &tmp_str, &addr_len));

        // broadcast address
        if ((br_addr = ifi->ifi_brdaddr) != NULL) printf("\tbroadcast address: %s\n", sa_ntop(br_addr, &tmp_str, &addr_len)); 

        // destination address
        if ((dst_addr = ifi->ifi_dstaddr) != NULL) printf("\tdestination address: %s\n", sa_ntop(dst_addr, &tmp_str, &addr_len)); 
        inter_index++; 
    }

    free_ifi_info(ifihead);

    // info for interface array
    printf("\n[INFO] Bound interfaces>\n");
    for(iter = 0; iter < inter_index; iter++) {
        printf("\n\tInterface #%d:\n", iter);
        log_debug("\t\t(DEBUG) sock_fd: %d\n", sock_data_info[iter].sock_fd);
        printf("\t\tIP Address: %s\n", sa_ntop(sock_data_info[iter].ip_addr, &tmp_str, &addr_len));
        // take care of different annotation of network and host, (ntohs, htons)
        printf("\t\tPort Number: %d\n", ntohs(((struct sockaddr_in *)sock_data_info[iter].ip_addr)->sin_port));
        printf("\t\tNetwork Mask: %s\n", sa_ntop(sock_data_info[iter].net_mask, &tmp_str, &addr_len));
        printf("\t\tSubnet Address: %s\n", sa_ntop(sock_data_info[iter].subn_addr, &tmp_str, &addr_len));
    }

    // use select for incoming connection
    fd_set f_s;
    int max_fd_count = 0;
    FD_ZERO(&f_s);

    // fork a child to handle incoming conn
    pid_t child_pid;

    struct sigaction act;
    act.sa_handler = sig_child;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
#ifdef SA_RESTART
    act.sa_flags |= SA_RESTART;
#endif
    if(sigaction(SIGCHLD, &act, NULL) < 0) {
        my_err_quit("[parent] signal handler error");
    }

    /*if(signal(SIGCHLD, sig_child) == SIG_ERR) {
        my_err_quit("[parent] signal handler error");
    }*/

    for( ; ; ) {
        printf("\n[INFO] Expecting upcoming datagram...\n");
        if(child_info_list != NULL) {
            printf("\tCurrent active children:\n");
            struct child_info * temp_pt = child_info_list;
            for( ; temp_pt != NULL;) {
                printf("\t\tchild #%u with initial SYN #%u\n", (uint32_t)temp_pt->pid, temp_pt->syn_init);
                temp_pt = temp_pt->next;
            }
        }
        max_fd_count = 0;
        for(iter = 0; iter < inter_index; iter++) {
            FD_SET(sock_data_info[iter].sock_fd, &f_s);
            max_fd_count= ((sock_data_info[iter].sock_fd > max_fd_count) ? sock_data_info[iter].sock_fd : max_fd_count) + 1;
        }
        // log_info("[DEBUG] max fd #: %d\n", max_fd_count);

        if(select(max_fd_count, &f_s, NULL, NULL, NULL) < 0) {
            if(errno == EINTR) {
                /* how to handle */
                printf("[INFO] Signal Caught! Interrupt Waiting LOOP!\n");
                continue;
            } else {
                my_err_quit("select error");
            }
        }
        // log_info("[DEBUG] select done\n");

        for(iter = 0; iter < inter_index; iter++) {
            if(FD_ISSET(sock_data_info[iter].sock_fd, &f_s)) {
                // log_info("[DEBUG] interface to be used: #%d\n", iter);
                // receive the client's first hand-shake, 1st SYN, 1st SYN
                recv_size = 0;

                if((recv_size = recvfrom(sock_data_info[iter].sock_fd, recv_dgram, (size_t) DATAGRAM_SIZE, 0, cli_addr, (socklen_t *) &cli_len)) < 0) {
                    my_err_quit("recvfrom error");
                }

                char filename[DATAGRAM_SIZE];

                parse_dgram(recv_dgram, &recv_hdr, filename, recv_size);
                // log_info("\t[DEBUG] Received datagram size: %d\n", recv_size);
                filename[recv_size - sizeof(struct tcp_header)] = 0; // with no padding
                // I should terminate the string by myself

                /* should deal with seq and flags during ensuring stability */
                /* check active child list to see if the new SYN is a dup one or not */
                struct child_info * temp_pt = child_info_list;
                uint8_t found = 0;
                for( ; temp_pt != NULL; ) {
                    if(temp_pt->syn_init == recv_hdr.seq) {
                        printf("\n[INFO] Duplicate SYN #%u detected\n", temp_pt->syn_init);
                        printf("\n[INFO] Send alarm of retransmission to corresponding child #%u\n", (uint32_t)temp_pt->pid);
                        found = 1;
                        kill(temp_pt->pid, SIGALRM);
                        break;
                    }
                    temp_pt = temp_pt->next;
                }
                if(found) {
                    // no need to continue this procedure, as its only a dup SYN
                    break;
                }

                printf("\n[INFO] Successfully connected from client with IP address: %s\n", sa_ntop(cli_addr, &tmp_str, &cli_len));
                printf("\tWith port #: %d\n", ntohs(((struct sockaddr_in *)cli_addr)->sin_port));
                printf("\tRequested filename: %s\n\n", filename);

                /* fork child to handle this specific request 
                 * and parent go on waiting for incoming clients
                 * */

                if((child_pid = fork()) == 0) { // child
                    //printf("\033[33m[INFO] Server child forked!\033[0m\n");
                    memcpy(chi_addr, sock_data_info[iter].ip_addr, sizeof(struct sockaddr));

                    int flags = -1, send_flag = 0, listen_fd = sock_data_info[iter].sock_fd;

                    flags = check_address(sock_data_info + iter, cli_addr);
                    printf("\n[DETECTED] ");
                    if(flags == FLAG_NON_LOCAL) {
                        printf("Client address doesn\'t belong to local network!\n");
                    } else if(flags == FLAG_LOCAL) {
                        printf("Client address is a local address!\n");
                        send_flag = MSG_DONTROUTE;
                    } else if(flags == FLAG_LOOP_BACK) {
                        printf("Client address is a loop-back address! (Server and Client are in the same machine)\n");
                        send_flag = MSG_DONTROUTE;
                    } else {
                        printf("There must be something wrong with check_address func!\n");
                        exit(1);
                    }

                    printf("\tServer: %s:%d\n", sa_ntop(chi_addr, &tmp_str, &addr_len), ntohs(((struct sockaddr_in *)chi_addr)->sin_port));
                    printf("\tClient: %s:%d\n", sa_ntop(cli_addr, &tmp_str, &addr_len), ntohs(((struct sockaddr_in *)cli_addr)->sin_port));

                    // Now child has its derived listening socket still open
                    // Time to create connection socket and bind it
                    if((conn_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
                        my_err_quit("socket error");
                    }

                    // set sock option for connection sock
                    on_flag = 1;
                    if(setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &on_flag, sizeof(on_flag)) < 0) {
                        my_err_quit("set socket option error");
                    }

                    // set port for conn
                    ((struct sockaddr_in *)chi_addr)->sin_family = AF_INET;
                    ((struct sockaddr_in *)chi_addr)->sin_port = htons(0); // for wildcard

                    // bind conn sock
                    if(bind(conn_fd, chi_addr, sizeof(struct sockaddr)) < 0) {
                        my_err_quit("bind error");
                    }

                    // use getsockname to get the bound ip and the ephemeral port for later use
                    if(getsockname(conn_fd, chi_addr, (socklen_t *) &chi_len) < 0) {
                        my_err_quit("getsockname error");
                    }

                    printf("\n[INFO] After binding the connection socket to the Server ip address by the child>\n");
                    printf("\tServer IP: %s\n", sa_ntop(chi_addr, &tmp_str, &addr_len));
                    printf("\tServer port: %d\n", ntohs(((struct sockaddr_in *)chi_addr)->sin_port));
                            
                    // should not treat the retransmitted SYN as the new incoming dgram
                    // connect the client via connection socket
                    if(connect(conn_fd, cli_addr, sizeof(struct sockaddr)) < 0) {
                        my_err_quit("connect error");
                    }

                    // tell the client the conn socket via listening socket
                    uint16_t port_to_tell = ((struct sockaddr_in *)chi_addr)->sin_port;
                    sent_size = sizeof(struct tcp_header) + sizeof(uint16_t);

                    printf("\n[INFO] Server child is sending the port of newly created connection socket back to the client>\n");
                    printf("\tport for server conn socket #: %d (to be sent to client)\n", ntohs(port_to_tell));

                    make_dgram(send_dgram, 
                            make_hdr(&send_hdr,
                                random(),
                                recv_hdr.seq + 1,
                                0, // no need to use rtt here
                                recv_hdr.tsopt,
                                HDR_ACK | HDR_SYN,
                                avail_win_sz), // available empty sender size
                            &port_to_tell,
                            sizeof(uint16_t),
                            &sent_size);
                    // log_info("\t[DEBUG] Sent datagram size: %d\n", sent_size);
                    // no ARQ needed here

                    signal(SIGALRM, sig_alarm); // for retransmission of 2nd hand shake
                    set_no_rtt_time_out();
handshake_2nd:
                    if((sent_size = sendto(listen_fd, send_dgram, sent_size, send_flag, 
                                    cli_addr, cli_len)) < 0) {
                        my_err_quit("sendto error");
                    }

                    if(no_rtt_time_out != no_rtt_init_time_out) { // this is a retransmission scenario
                        // should send the dgram via both listening and connection sock
                        if((sent_size = send(conn_fd, send_dgram, sent_size, send_flag/*, cli_addr, cli_len*/)) < 0) {
                            my_err_quit("send error");
                        }
                    }

                    alarm(no_rtt_time_out);
                    if(sigsetjmp(jmpbuf, 1) != 0) {
                        if(no_rtt_time_out < no_rtt_max_time_out) {
                            printf("\n[INFO] Resend ACK+SYN datagram after retransmission time-out %d s\n", no_rtt_time_out);
                            no_rtt_time_out += 1;
                            goto handshake_2nd;
                        }
                        else {
                            printf("\n[INFO] Retransmission time-out reaches the limit: %d s, giving up...\n", no_rtt_max_time_out);
                            exit(1);
                        }
                    }

                    /*
                     * Note that this implies that, in the event of the server timing out, 
                     * it should retransmit two copies of its ‘ephemeral port number’ message, 
                     * one on its ‘listening’ socket and the other on its ‘connection’ socket (why?).
                     * for that, server doesn't know client's sock is currently connecting to which sock of server
                     * */
                    // 3rd handshake, connection socket receive ACK from client
                    do {
                        if((recv_size = recvfrom(conn_fd, recv_dgram, (size_t) DATAGRAM_SIZE, 0, cli_addr, (socklen_t *) &cli_len)) < 0) {
                            my_err_quit("recvfrom error");
                        }

                        printf("\n[INFO] Received a datagram from client, supposed to be the 3rd handshake>\n");
                        parse_dgram(recv_dgram, &recv_hdr, NULL, recv_size);
                        // log_info("\t[DEBUG] Received datagram size: %d\n", recv_size);

                        // print_dgram("Received", &recv_hdr);
                        if(recv_hdr.ack == ntohl(send_hdr.seq) + 1) {
                            printf("\t[INFO] 3rd handshake succeeded!\n");
                            printf("\t[INFO] Listening socket of server child gonna close!\n");
                            close(listen_fd);
                        } else {
                            printf("\t[ERROR] Wrong client ACK #: %u while (sent) SEQ + 1 #: %u expected!\n", recv_hdr.ack, ntohl(send_hdr.seq)+1);
                        } // if wrong ack, then we should just drop the dgram and re-receive
                    } while(recv_hdr.ack != ntohl(send_hdr.seq) + 1);
                    // we don't worry because we have re-trans and time-out mechanism

                    alarm(0); // no need to re-trans, disable alarm

                    // sliding window mechanism
                    // update window size according to advertised client receiver window size
                    sli_win_sz = (config_serv.sli_win_sz < recv_hdr.window_size)?config_serv.sli_win_sz:recv_hdr.window_size;
                    printf("\n[INFO] After receiver advertising, sender sliding window size: %u\n", sli_win_sz);
                    sli_win_sz = (sli_win_sz < cwnd) ? sli_win_sz : cwnd;
                    printf("[INFO] But due to the need of slow start, sender sliding window size is modified to: %u\n", sli_win_sz);

                    FILE * data_file = fopen(filename, "r");
                    if(data_file == NULL) {
                        printf("\n[ERROR] File %s does not exist!\n", filename);
                        exit(1);
                    }

                    // int seq_num = 0;
                    int read_size = -1; // didn't start read file
                    uint8_t retrans_flag = 1;
                    printf("\n[INFO] Server child is going to send file \"%s\"!\n", filename);
                    signal(SIGALRM, sig_alarm); // for retransmission of data parts

                    uint32_t last_client_seq = recv_hdr.seq;

                    while(1) {

                        if(read_size != 0) {
                            // load file content into window buffer
                            uint32_t sli_window_index = sent_not_ack;

                            uint32_t idx; 
                            for(idx = 1; idx <= sli_win_sz; idx++) { // though obviously bigger than needed to be sent
                                log_debug("[DEBUG] current idx: %u\n", idx);
                                if(idx + (sent_not_ack - window_start) > sli_win_sz) {
                                    window_end = sli_window_index - 1;
                                    break;
                                }
                                int prev_read_size = read_size;
                                read_size = fread(sli_win[sli_window_index % config_serv.sli_win_sz].data_buf, sizeof(uint8_t), DATAGRAM_SIZE - sizeof(struct tcp_header), data_file);
                                sli_win[sli_window_index % config_serv.sli_win_sz].data_buf[read_size] = 0;
                                if(read_size == 0) {// ==0 means read to the end of file
                                    window_end = sli_window_index - 1;
                                    printf("\n[INFO] Get to the end of data file! SEQ of the last datagram would be #%d\n", sli_win[window_end % config_serv.sli_win_sz].seq);
                                    break;
                                }
                                sli_win[sli_window_index % config_serv.sli_win_sz].data_sz = read_size;
                                // the first time to read the file
                                if(prev_read_size == -1) {
                                    sli_win[sli_window_index % config_serv.sli_win_sz].seq = recv_hdr.ack;
                                }
                                else {
                                    // last dgram seq + 1
                                    sli_win[sli_window_index % config_serv.sli_win_sz].seq = \
                                                                    sli_win[(sli_window_index - 1 + config_serv.sli_win_sz) % config_serv.sli_win_sz].seq + 1;
                                }
                                rtt_init(&sli_win[sli_window_index % config_serv.sli_win_sz].rtt);
                                sli_win[sli_window_index % config_serv.sli_win_sz].ack_times = 0;
                                window_end = sli_window_index; // update window end
                                sli_window_index = sli_window_index + 1;

                            }
                            sli_win_sz = (window_end - window_start + config_serv.sli_win_sz) % config_serv.sli_win_sz + 1;
                            avail_win_sz = config_serv.sli_win_sz - sli_win_sz; // subtract used win size
                            log_debug("\n[DEBUG] read_size: %d | window start: %u | sent not ack: %u | window end: %u | current sliding window size: %u | available window size: %u\n", read_size, window_start, sent_not_ack, window_end, sli_win_sz, avail_win_sz);

                            if(avail_win_sz == 0) {
                                printf("\n[INFO] Sender sliding window is full!\n");
                            }

                            sli_window_index = sent_not_ack;
                            // int num = ((window_end - sent_not_ack + config_serv.sli_win_sz) % config_serv.sli_win_sz) + 1;
                            int num = 1;
                            printf("\n[INFO] Sending window gonna send %d Datagrams!\n", idx - num);
                            while(num < idx) {
                                // sent_size = sizeof(struct tcp_header) + read_size;

                                // printf("\n\t[INFO] going to send part #%d: %s\n", seq_num, file_buf);
                                printf("\t[INFO] Sending window gonna send Datagram seq #%u\n", sli_win[sli_window_index % config_serv.sli_win_sz].seq);
                                make_dgram(send_dgram,
                                        make_hdr(&send_hdr,
                                            sli_win[sli_window_index % config_serv.sli_win_sz].seq,
                                            last_client_seq, // as ack = seq + 1, only when responding to SYN or data payload
                                            rtt_ts(&sli_win[sli_window_index % config_serv.sli_win_sz].rtt),
                                            0, // recv_hdr.tsopt, no use of using the client timestamp
                                            0,
                                            avail_win_sz),
                                        sli_win[sli_window_index % config_serv.sli_win_sz].data_buf,
                                        sli_win[sli_window_index % config_serv.sli_win_sz].data_sz,
                                        &sent_size); /* TODO */ /* ARQ */

                                if((sent_size = send(conn_fd, send_dgram, sent_size, send_flag/*, cli_addr, cli_len*/)) < 0) {
                                    my_err_quit("send error");
                                }

                                // use RTT mechanism
                                // init RTT counter for every dgram
                                rtt_newpack(&sli_win[sli_window_index % config_serv.sli_win_sz].rtt);

                                printf("\t[INFO] Gonna emit the RTT timer for datagram with SEQ %u\n", sli_win[sli_window_index % config_serv.sli_win_sz].seq);
                                // alarm(rtt_start(&sli_win[sli_window_index % config_serv.sli_win_sz].rtt));
                                struct timeval tv1;
                                tv1.tv_sec = rtt_start(&sli_win[sli_window_index % config_serv.sli_win_sz].rtt) / 1000;
                                tv1.tv_usec = (rtt_start(&sli_win[sli_window_index % config_serv.sli_win_sz].rtt) % 1000) * 1000;

                                if(timer_queue_front == NULL) {
                                    struct timeval tv2;
                                    tv2.tv_sec = tv2.tv_usec = 0;
                                    struct itimerval it;
                                    it.it_interval = tv2;
                                    it.it_value = tv1;
                                    setitimer(ITIMER_REAL, &it, NULL);
                                }

                                timer_queue_push(tv1);

                                sli_window_index = sli_window_index + 1;
                                num++;
                            }

                            sent_not_ack = window_end + 1; // update status of dgram in sending window
                            // printf("[DEBUG] current sent not ack %d!\n", sent_not_ack);

                        }

                        if(sigsetjmp(jmpbuf, 1) != 0) {
                            if(rtt_timeout(&sli_win[window_start % config_serv.sli_win_sz].rtt) == 0) {
                                // retransmit sent_not_ack data, from window_start
                                if(retrans_flag == 1) { // retransmission should be enabled
                                    printf("\n[INFO] TIMEOUT, retransmit Dgram with seq #%d\n", sli_win[window_start % config_serv.sli_win_sz].seq);
                                    cwnd >>= 1;
                                    printf("\t[INFO] TIMEOUT so congestion is detected, so cwnd / 2 -> %d\n", cwnd);
                                    if(cwnd == 0) cwnd = 1;
                                    make_dgram(send_dgram,
                                            make_hdr(&send_hdr,
                                                sli_win[window_start % config_serv.sli_win_sz].seq,
                                                recv_hdr.seq, // as ack = seq + 1, only when responding to SYN or data payload
                                                rtt_ts(&sli_win[window_start % config_serv.sli_win_sz].rtt),
                                                0, // recv_hdr.tsopt, no use of using the client timestamp
                                                0,
                                                avail_win_sz),
                                            sli_win[window_start % config_serv.sli_win_sz].data_buf,
                                            sli_win[window_start % config_serv.sli_win_sz].data_sz,
                                            &sent_size);

                                    if((sent_size = send(conn_fd, send_dgram, sent_size, send_flag/*, cli_addr, cli_len*/)) < 0) {
                                        my_err_quit("send error");
                                    }

                                } else {
                                    printf("\n[INFO] TIMEOUT, but retransmision disabled! So, do nothing!\n");
                                }
                                printf("\t[INFO] Gonna re-emit the RTT timer for datagram with SEQ %u\n", sli_win[window_start % config_serv.sli_win_sz].seq);
                                // alarm(rtt_start(&sli_win[window_start % config_serv.sli_win_sz].rtt)); // set retransmission for newly retransmitted dgram

                                struct timeval tv1, delta;
                                tv1.tv_sec = rtt_start(&sli_win[window_start % config_serv.sli_win_sz].rtt) / 1000;
                                tv1.tv_usec = (rtt_start(&sli_win[window_start % config_serv.sli_win_sz].rtt) % 1000) * 1000;

                                struct timer_info * timer1 = timer_queue_pop(), * timer2 = timer1->next;
                                if(timer2 != NULL) {
                                    delta.tv_sec = timer2->set_time.tv_sec + timer2->set_time.tv_sec \
                                                   - timer1->set_time.tv_sec - timer1->delay.tv_sec;
                                    delta.tv_usec = timer2->set_time.tv_usec + timer2->set_time.tv_usec \
                                                    - timer1->set_time.tv_usec - timer1->delay.tv_usec;
                                } else {
                                    delta = tv1;
                                }
                                if(delta.tv_sec >= 3) {
                                    delta.tv_sec = 3;
                                    delta.tv_usec = 0;
                                }
                                if(delta.tv_sec < 1) {
                                    delta.tv_sec = 1;
                                    delta.tv_usec = 0;
                                }
                                log_debug("[DEBUG] delta: %d s, %d us\n", (int)delta.tv_sec, (int)delta.tv_usec);
                                struct timeval tv2;
                                tv2.tv_sec = tv2.tv_usec = 0;
                                struct itimerval it;
                                it.it_interval = tv2;
                                it.it_value = delta;
                                setitimer(ITIMER_REAL, &it, NULL);

                                timer_queue_push(tv1);

                                free(timer1);
                            }
                            else {
                                printf("\n[ERROR] Retransmission time-out reaches the limit of retransmission time: %d, giving up...\n", RTT_MAXNREXMT);
                                exit(1);
                            }
                        }

                        do {
                            if((recv_size = recvfrom(conn_fd, recv_dgram, (size_t) DATAGRAM_SIZE, 0, cli_addr, (socklen_t *) &cli_len)) < 0) {
                                my_err_quit("recvfrom error");
                            }

                            // printf("\n\t[INFO] Received ACK from client after sending part #%d of file %s!\n", seq_num, filename);
                            parse_dgram(recv_dgram, &recv_hdr, NULL, recv_size);
                            // log_debug("\t[DEBUG] Received datagram size: %d\n", recv_size);
                            // printf("\n[DEBUG] !!! %d\n", sli_win[window_end % config_serv.sli_win_sz].seq + 1);
                            if(read_size == 0 && recv_hdr.ack == sli_win[window_end % config_serv.sli_win_sz].seq + 1) {
                                printf("[INFO] ACK for last datagram received!\n");
                                break;
                            }

                            cwnd++;
                            printf("\n[INFO] ACK received and congestion isn't detected, so cwnd +1 -> %d\n", cwnd);

                            if(recv_hdr.window_size == 0) {
                                printf("\n[INFO] Receiver sliding window is full! ACK dropped! Waiting for window updates!\n");
                                printf("\n[INFO] Retransmission disabled!\n");
                                if(retrans_flag == 1) {
                                    // alarm(0);
                                    retrans_flag = 0;
                                }
                            } else { // should enable retransmission
                                // if dup ack
                                // fast retransmit
                                // if(recv_hdr.ack <

                                // if dup ack, then update window when ack is valid
                                // window slide forward
                                int move_forward = recv_hdr.ack - sli_win[window_start % config_serv.sli_win_sz].seq;

                                if(move_forward <= 0) {
                                    printf("\n[INFO] Received dgram ack #%u is smaller than the \"sent but not ack-ed\" dgram seq #%u, ack dropped\n", \
                                            recv_hdr.ack, sli_win[window_start % config_serv.sli_win_sz].seq);
                                    break; // should be?
                                }

                                last_client_seq = recv_hdr.seq;


                                log_debug("\n[DEBUG] move forward: %d\n", move_forward);
                                uint32_t num;
                                /*for(num = 0; num < move_forward; num++) {
                                    printf("\n[INFO] Gonna disable the RTT timer for datagram with SEQ %u\n", sli_win[(window_start + num) % config_serv.sli_win_sz].seq);
                                    //alarm(0); // disable alarm for sent dgram
                                }*/

                                window_start = window_start + move_forward;
                                avail_win_sz += move_forward;
                                if(avail_win_sz > config_serv.sli_win_sz) {
                                    avail_win_sz = config_serv.sli_win_sz;
                                }

                                sli_win_sz -= move_forward;
                                sli_win_sz = (sli_win_sz + avail_win_sz > recv_hdr.window_size) ?\
                                             recv_hdr.window_size : sli_win_sz + avail_win_sz;

                                printf("\n[INFO] after comparing the client acknowledged receiver window size and server's sender window size, sliding window has been modified to be %d\n", sli_win_sz);
                                //if(cwnd < SSTHRESH) {
                                //    cwnd *= 2;
                                //} else {
                                //}
                                printf("\n[INFO] last window of data successfully acknowledged, congestion window doubled: %u\n", cwnd);
                                sli_win_sz = (sli_win_sz < cwnd)? sli_win_sz : cwnd;
                                printf("\n[INFO] according to congestion window size, the real sliding window size for next window of data should be %d\n", sli_win_sz);
                                log_debug("\n[DEBUG][A] read_size: %d | window start: %u | sent not ack: %u | window end: %u | current sliding window size: %u | available window size: %u\n", 
                                        read_size, window_start, sent_not_ack, window_end, sli_win_sz, avail_win_sz);

                                if(retrans_flag == 0) {
                                    retrans_flag = 1;
                                    // alarm(no_rtt_time_out);
                                    printf("\n[INFO] Receiver sliding window is open now! Enable retransmission!\n");
                                    // alarm(rtt_start(&rtt));
                                }

                                if(recv_hdr.tsecr != 0) {
                                    num = window_start;
                                    while(num != sent_not_ack) {
                                        //printf("\n[INFO] Gonna re-emit the RTT timer for datagram with SEQ %u\n", sli_win[num % config_serv.sli_win_sz].seq);
                                        rtt_stop(&sli_win[num % config_serv.sli_win_sz].rtt, rtt_ts(&sli_win[num % config_serv.sli_win_sz].rtt) - recv_hdr.tsecr); // update rtt after every received packet
                                        num = num + 1;
                                    }
                                    //which has server sending timestamp
                                }
                            }

                        } while(recv_hdr.window_size == 0); // || recv_hdr.ack != ntohl(send_hdr.seq) + 1);

                        if(recv_hdr.ack == sli_win[window_end % config_serv.sli_win_sz].seq + 1 && read_size == 0) {
                            printf("\n[INFO] all data sent done!\n");
                            printf("\n[INFO] File %s sent complete!\n", filename);
                            int to_close = 0;

                            // server ---FIN---> client
                            // server <---FIN/ACK--- client
                            // server ---ACK---> client

                            printf("\n[INFO] Server child is gonna tell client to finish this work!\n");

                            make_dgram(send_dgram, 
                                    make_hdr(&send_hdr,
                                        recv_hdr.ack,
                                        recv_hdr.seq,
                                        0, // no need to use rtt here
                                        0,
                                        HDR_FIN,
                                        avail_win_sz), // available empty sender size
                                    NULL,
                                    0,
                                    &sent_size);
                            // log_info("\t[DEBUG] Sent datagram size: %d\n", sent_size);
                            // no ARQ needed here

                            signal(SIGALRM, sig_alarm); // for retransmission of 2nd hand shake
                            set_no_rtt_time_out();
syn_to_client:
                            if((sent_size = send(conn_fd, send_dgram, sent_size, send_flag/*, cli_addr, cli_len*/)) < 0) {
                                my_err_quit("send error");
                            }

                            alarm(no_rtt_time_out);
                            if(sigsetjmp(jmpbuf, 1) != 0) {
                                if(no_rtt_time_out < no_rtt_max_time_out) {
                                    printf("\n[INFO] Resend FIN datagram after retransmission time-out %d s\n", no_rtt_time_out);
                                    no_rtt_time_out += 1;
                                    goto syn_to_client;
                                }
                                else {
                                    printf("\n[ERROR] Retransmission time-out reaches the limit: %d s, giving up...\n", no_rtt_max_time_out);
                                    exit(1);
                                }
                            }

                            do {
                                if((recv_size = recvfrom(conn_fd, recv_dgram, (size_t) DATAGRAM_SIZE, 0, cli_addr, (socklen_t *) &cli_len)) < 0) {
                                    my_err_quit("recvfrom error");
                                }

                                parse_dgram(recv_dgram, &recv_hdr, NULL, recv_size);
                                // log_info("\t[DEBUG] Received datagram size: %d\n", recv_size);

                                // print_dgram("Received", &recv_hdr);
                                if(recv_hdr.ack == ntohl(send_hdr.seq) + 1) {
                                    printf("\t[INFO] Client SYN+ACK received! Gonna close!\n");
                                    to_close = 1;
                                    break;
                                } else {
                                    printf("\n[ERROR] Wrong client ACK #: %u, (sent) SEQ + 1 #: %u expected!\n", recv_hdr.ack, ntohl(send_hdr.seq)+1);
                                } // if wrong ack, then we should just drop the dgram and re-receive
                            } while(recv_hdr.ack != ntohl(send_hdr.seq) + 1);
                            // we don't worry because we have re-trans and time-out mechanism

                            alarm(0); // no need to re-trans, disable alarm

                            if(to_close) {
                                printf("\n[INFO] Server child is gonna send client the last ACK!\n");

                                make_dgram(send_dgram, 
                                        make_hdr(&send_hdr,
                                            recv_hdr.ack,
                                            recv_hdr.seq + 1,
                                            0, // no need to use rtt here
                                            0,
                                            HDR_ACK,
                                            avail_win_sz), // available empty sender size
                                        NULL,
                                        0,
                                        &sent_size);
                                // log_info("\t[DEBUG] Sent datagram size: %d\n", sent_size);

                                if((sent_size = send(conn_fd, send_dgram, sent_size, send_flag/*, cli_addr, cli_len*/)) < 0) {
                                    my_err_quit("send error");
                                }
                                break;
                            }
                        }

                    }

                    printf("\n[INFO] Connection socket gonna close!\n");
                    close(conn_fd);

                    // data transfer finished, end child process
                    goto finish_all;

                } else {
                    printf("[INFO] Server child #%u forked!\n", (uint32_t)child_pid);

                    // insert the new child with its init syn into list
                    struct child_info * cur_child = malloc(sizeof(struct child_info));
                    cur_child->pid = child_pid;
                    cur_child->syn_init = recv_hdr.seq;
                    cur_child->next = NULL;

                    if(child_info_list == NULL) {
                        cur_pt = child_info_list = cur_child;
                    } else {
                        cur_pt->next = cur_child;
                        cur_pt = cur_pt->next;
                    }

                    break; // parent
                }

            }
        }
    }

finish_all:
    // garbage collection
    printf("\n[INFO] Process gonna close...\n");
    printf("\n[INFO] Cleaning junk...\n");

    if(ip_addr != NULL) free(ip_addr);
    if(net_mask != NULL) free(net_mask);
    if(sub_net != NULL) free(sub_net);
    if(cli_addr != NULL) free(cli_addr);

    for(iter = 0; iter < inter_index; iter++) {
        free(sock_data_info[iter].ip_addr);
        free(sock_data_info[iter].net_mask);
        free(sock_data_info[iter].subn_addr);
    }

    for( ; child_info_list != NULL; ) {
        struct child_info * temp = child_info_list;
        child_info_list = child_info_list->next;
        free(temp);
    }

    destroy_window();

    return 0;
}
