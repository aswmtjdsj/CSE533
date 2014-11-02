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
    fscanf(conf_file, " %d", &(conf->sli_win_sz));
    fclose(conf_file);

    return 0;
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

void print_hdr(struct tcp_header * hdr) {
    log_info("\t[DEBUG] tcp header\n");
    log_info("\t[DEBUG] seq#: %u\n", hdr->seq);
    log_info("\t[DEBUG] datagram ack#: %u\n", hdr->ack);
    log_info("\t[DEBUG] sender timestamp: %u\n", hdr->tsopt);
    log_info("\t[DEBUG] receiver timestamp: %u\n", hdr->tsecr);
    if(hdr->flags & HDR_ACK) {
        log_info("\t[DEBUG] flagged with: ACK\n");
    }
    if(hdr->flags & HDR_SYN) {
        log_info("\t[DEBUG] flagged with: SYN\n");
    }
    if(hdr->flags & HDR_FIN) {
        log_info("\t[DEBUG] flagged with: FIN\n");
    }
    log_info("\t[DEBUG] window size: %u\n", hdr->window_size);
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
    printf("[parent] child process %d terminated\n", pid);
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
    
    print_hdr(hdr);
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

    // for RTT mechanism
    struct rtt_info rtt;
    rtt_init(&rtt);

    // get configuration
    printf("[CONFIG]\n");
    read_serv_conf(&config_serv);
    print_serv_conf(&config_serv);

    // IFI INFO
    printf("[IFI] Info of %s:\n", "Server");
    for( ifihead = ifi = get_ifi_info(AF_INET, 1); ifi != NULL; ifi = ifi->ifi_next) {
        // Interface
        printf("%s: ", ifi->ifi_name);
        if (ifi->ifi_index != 0) printf("(%d) ", ifi->ifi_index);
        print_ifi_flags(ifi);
        if (ifi->ifi_mtu != 0) printf("  MTU: %d\n", ifi->ifi_mtu);
        if ((ip_addr = ifi->ifi_addr) != NULL) printf("  IP address: %s\n", sa_ntop(ip_addr, &tmp_str, &addr_len)); 

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
        if ((net_mask = ifi->ifi_ntmaddr) != NULL) printf("  network mask: %s\n", sa_ntop(net_mask, &tmp_str, &addr_len)); 

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
        if ((br_addr = ifi->ifi_brdaddr) != NULL) printf("  broadcast address: %s\n", sa_ntop(br_addr, &tmp_str, &addr_len)); 

        // destination address
        if ((dst_addr = ifi->ifi_dstaddr) != NULL) printf("  destination address: %s\n", sa_ntop(dst_addr, &tmp_str, &addr_len)); 

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
        // take care of different annotation of network and host, (ntohs, htons)
        printf("\tPort Number: %d\n", ntohs(((struct sockaddr_in *)sock_data_info[iter].ip_addr)->sin_port));
        printf("\tNetwork Mask: %s\n", sa_ntop(sock_data_info[iter].net_mask, &tmp_str, &addr_len));
        printf("\tSubnet Address: %s\n", sa_ntop(sock_data_info[iter].subn_addr, &tmp_str, &addr_len));
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
            printf("[INFO] Current active children:\n");
            struct child_info * temp_pt = child_info_list;
            for( ; temp_pt != NULL;) {
                printf("\t child #%d with initial SYN #%u\n", temp_pt->pid, temp_pt->syn_init);
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
                log_info("\t[DEBUG] Received datagram size: %d\n", recv_size);
                filename[recv_size - sizeof(struct tcp_header)] = 0; // with no padding
                // I should terminate the string by myself

                /* should deal with seq and flags during ensuring stability */
                /* check active child list to see if the new SYN is a dup one or not */
                struct child_info * temp_pt = child_info_list;
                uint8_t found = 0;
                for( ; temp_pt != NULL; ) {
                    if(temp_pt->syn_init == recv_hdr.seq) {
                        printf("\n[INFO] Duplicate SYN #%u detected\n", temp_pt->syn_init);
                        printf("\n[INFO] Send alarm of retransmission to corresponding child #%d\n", temp_pt->pid);
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

                printf("\nSuccessfully connected from client with IP address: %s\n", sa_ntop(cli_addr, &tmp_str, &cli_len));
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

                    printf("[INFO] Server: %s:%d\n", sa_ntop(chi_addr, &tmp_str, &addr_len), ntohs(((struct sockaddr_in *)chi_addr)->sin_port));
                    printf("[INFO] Client: %s:%d\n", sa_ntop(cli_addr, &tmp_str, &addr_len), ntohs(((struct sockaddr_in *)cli_addr)->sin_port));
                    printf("\n");

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

                    printf("[INFO] After binding the connection socket to the Server ip address by the child>\n");
                    printf("\tServer IP: %s\n", sa_ntop(chi_addr, &tmp_str, &addr_len));
                    printf("\tServer port: %d\n", ntohs(((struct sockaddr_in *)chi_addr)->sin_port));
                    printf("\n");
                            
                    // TODO
                    // should not treat the retransmitted SYN as the new incoming dgram

                    // connect the client via connection socket
                    if(connect(conn_fd, cli_addr, sizeof(struct sockaddr)) < 0) {
                        my_err_quit("connect error");
                    }

                    // tell the client the conn socket via listening socket
                    uint16_t port_to_tell = ((struct sockaddr_in *)chi_addr)->sin_port;
                    sent_size = sizeof(struct tcp_header) + sizeof(uint16_t);

                    printf("[INFO] Server child is sending the port of newly created connection socket back to the client>\n");
                    printf("\t[INFO]port for server conn socket #: %d (to be sent to client)\n", ntohs(port_to_tell));

                    make_dgram(send_dgram, 
                            make_hdr(&send_hdr,
                                random(),
                                recv_hdr.seq + 1,
                                rtt_ts(&rtt),
                                recv_hdr.tsopt,
                                HDR_ACK | HDR_SYN,
                                0),
                            &port_to_tell,
                            sizeof(uint16_t),
                            &sent_size);
                    log_info("\t[DEBUG] Sent datagram size: %d\n", sent_size);
                    /* TODO */ /* ARQ */

                    signal(SIGALRM, sig_alarm); // for retransmission of 2nd hand shake
                    set_no_rtt_time_out();
handshake_2nd:
                    if((sent_size = sendto(listen_fd, send_dgram, sent_size, send_flag, 
                                    cli_addr, cli_len)) < 0) {
                        my_err_quit("sendto error");
                    }

                    if(no_rtt_time_out != no_rtt_init_time_out) { // this is a retransmission scenario
                        // should send the dgram via both listening and connection sock
                        if((sent_size = sendto(conn_fd, send_dgram, sent_size, send_flag, 
                                        cli_addr, cli_len)) < 0) {
                            my_err_quit("sendto error");
                        }
                    }

                    alarm(no_rtt_time_out);
                    if(sigsetjmp(jmpbuf, 1) != 0) {
                        if(no_rtt_time_out < no_rtt_max_time_out) {
                            printf("Resend ACK+SYN datagram after retransmission time-out %d s\n", no_rtt_time_out);
                            no_rtt_time_out += 3;
                            goto handshake_2nd;
                        }
                        else {
                            printf("Retransmission time-out reaches the limit: %d s, giving up...\n", no_rtt_max_time_out);
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

                        printf("\n[INFO] It supposed to be the 3rd handshake while connection socket received a datagram from client>\n");
                        parse_dgram(recv_dgram, &recv_hdr, NULL, recv_size);
                        log_info("\t[DEBUG] Received datagram size: %d\n", recv_size);

                        // print_dgram("Received", &recv_hdr);
                        if(recv_hdr.ack == ntohl(send_hdr.seq) + 1) {
                            printf("\t[INFO] 3rd handshake succeeded! Listening socket of server child gonna close!\n");
                            close(listen_fd);
                        } else {
                            printf("\t[ERROR] Wrong ACK #: %u, (sent) seq + 1 #: %u expected!\n", recv_hdr.ack, ntohl(send_hdr.seq)+1);
                        } // if wrong ack, then we should just drop the dgram and re-receive
                        printf("\n");
                    } while(recv_hdr.ack != ntohl(send_hdr.seq) + 1);
                    // we don't worry because we have re-trans and time-out mechanism

                    alarm(0); // no need to re-trans, disable alarm
                    rtt_stop(&rtt, rtt_ts(&rtt) - recv_hdr.tsecr); // update rtt after every received packet
                    //which has server sending timestamp

                    // data transfer
                    // TODO

                    FILE * data_file = fopen(filename, "r");
                    if(data_file == NULL) {
                        printf("[ERROR] File %s does not exist!\n", filename);
                        exit(1);
                    }

                    char file_buf[DATAGRAM_SIZE];
                    int seq_num = 0;
                    int read_size = 0;
                    uint8_t retrans_flag = 1;
                    printf("[INFO] Server child is going to send file \"%s\"!\n", filename);
                    while((read_size = fread(file_buf, sizeof(uint8_t), DATAGRAM_SIZE - sizeof(struct tcp_header), data_file)) != 0) {

                        file_buf[read_size] = 0;
                        ++seq_num;
                        sent_size = sizeof(struct tcp_header) + read_size;

                        printf("\n\t[INFO] going to send part #%d: %s\n", seq_num, file_buf);
                        make_dgram(send_dgram,
                                make_hdr(&send_hdr,
                                    recv_hdr.ack,
                                    recv_hdr.seq, // as ack = seq + 1, only when responding to SYN or data payload
                                    rtt_ts(&rtt),
                                    recv_hdr.tsopt,
                                    0,
                                    0), /* TODO */
                                file_buf,
                                read_size,
                                &sent_size); /* TODO */ /* ARQ */
                        printf("\t[DEBUG] Sent datagram size: %d\n", sent_size);

                        signal(SIGALRM, sig_alarm); // for retransmission of data parts
                        // set_no_rtt_time_out();
                        // use RTT mechanism
                        // init RTT counter for every dgram
                        rtt_newpack(&rtt);
file_trans_again:
                        if((sent_size = sendto(conn_fd, send_dgram, sent_size, send_flag, 
                                        cli_addr, cli_len)) < 0) {
                            my_err_quit("sendto error");
                        }

                        if(retrans_flag == 1) { // enable retransmission
                            // alarm(no_rtt_time_out);
                            alarm(rtt_start(&rtt));
                        }

                        if(sigsetjmp(jmpbuf, 1) != 0) {
                            /*if(no_rtt_time_out < no_rtt_max_time_out) {
                                printf("\t[INFO] Resend #%d part of file %s after retransmission time-out %d s\n", seq_num, filename, no_rtt_time_out);
                                no_rtt_time_out += 3;
                                goto file_trans_again;
                            }*/
                            if(rtt_timeout(&rtt) == 0) {
                                printf("\t[INFO] Resend #%d part of file %s after retransmission time-out %d s\n", seq_num, filename, rtt.rtt_rto / 1000);
                                goto file_trans_again;
                            }
                            else {
                                printf("[ERROR] Retransmission time-out reaches the limit of retransmission time: %d, giving up...\n", RTT_MAXNREXMT);
                                exit(1);
                            }
                        }

                        do {
                            if((recv_size = recvfrom(conn_fd, recv_dgram, (size_t) DATAGRAM_SIZE, 0, cli_addr, (socklen_t *) &cli_len)) < 0) {
                                my_err_quit("recvfrom error");
                            }

                            printf("\n\t[INFO] Received ACK from client after sending part #%d of file %s!\n", seq_num, filename);
                            parse_dgram(recv_dgram, &recv_hdr, NULL, recv_size);
                            printf("\t[DEBUG] Received datagram size: %d\n", recv_size);

                            if(recv_hdr.window_size == 0) {
                                printf("\t[ERROR] Receiver sliding window is full! ACK dropped! Waiting for window updates!\n");
                                printf("\t[INFO] Retransmission disabled!\n");
                                if(retrans_flag == 1) {
                                    alarm(0);
                                    retrans_flag = 0;
                                }
                            } else { // should enable retransmission
                                if(retrans_flag == 0) {
                                    retrans_flag = 1;
                                    // alarm(no_rtt_time_out);
                                    printf("\t[INFO] Receiver sliding window is open now! Enable retransmission!\n");
                                    alarm(rtt_start(&rtt));
                                }
                                if(recv_hdr.ack == ntohl(send_hdr.seq) + 1) {
                                    printf("\t[INFO] Part #%d of file %s correctly sent!\n", seq_num, filename);
                                } else {
                                    printf("\tWrong ACK #: %u, (sent) seq + 1 #: %u expected!\n", recv_hdr.ack, ntohl(send_hdr.seq)+1);
                                }
                            }
                        } while(recv_hdr.window_size == 0 || recv_hdr.ack != ntohl(send_hdr.seq) + 1);

                        alarm(0); // disable alarm
                        rtt_stop(&rtt, rtt_ts(&rtt) - recv_hdr.tsecr); // update rtt after every received packet
                        //which has server sending timestamp
                    }

                    printf("\n[INFO] File %s sent complete!\n", filename);
                    printf("[INFO] Connection socket gonna close!\n");
                    close(conn_fd);

                    // data transfer finished, end child process
                    exit(0);

                } else {
                    printf("[INFO] Server child #%d forked!\n", child_pid);

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

    // garbage collection
    printf("Cleaning junk...\n");

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

    return 0;
}
