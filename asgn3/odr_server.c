#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "log.h"
#include "mainloop.h"
#include "const.h"
#include "odr_protocol.h"
#include "msg_api.h"

struct co_table {
    uint16_t port;
    char sun_path[SUN_PATH_MAX_LEN];
	//struct timeval tv;
	//struct timeval remain_tv;
    void * timer;
    struct co_table * next;
};

struct odr_msg_hdr {
    uint32_t src_ip;
    uint16_t src_port;
    uint32_t dst_ip;
    uint16_t dst_port;
    uint16_t msg_len;
};

struct bound_app {
    void * fh;
    void * op;
};

struct bound_odr {
    void * fd_p;
    void * ml;
};

struct co_table * table_head;

struct odr_msg_hdr * make_odr_msg_hdr(struct odr_msg_hdr * hdr, uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port, int len) {

    if(hdr == NULL) {
        hdr = malloc(sizeof(struct odr_msg_hdr));
    }

    // all network byte order
    hdr->src_ip = src_ip;
    hdr->src_port = src_port;
    hdr->dst_ip = dst_ip;
    hdr->dst_port = dst_port;
    hdr->msg_len = len;

    return hdr;
}

void make_odr_msg(uint8_t * odr_msg, struct odr_msg_hdr * hdr, void * payload, int payload_len, int * odr_msg_size) {
    memcpy(odr_msg, hdr, sizeof(struct odr_msg_hdr));
    memcpy(odr_msg + sizeof(struct odr_msg_hdr), payload, payload_len);
    *odr_msg_size = sizeof(struct odr_msg_hdr) + payload_len;

    char msg_debug[MSG_MAX_LEN];
    strncpy(msg_debug, payload, payload_len);
    msg_debug[payload_len] = 0;

    char src_ip_p[IP_P_MAX_LEN], dst_ip_p[IP_P_MAX_LEN];
    strcpy(src_ip_p, inet_ntoa((struct in_addr){hdr->src_ip}));
    strcpy(dst_ip_p, inet_ntoa((struct in_addr){hdr->dst_ip}));

    log_debug("odr_msg: {hdr: {src_ip: \"%s\", src_port: %u, dst_ip: \"%s\", dst_port: %u, msg_len: %d}, payload: \"%s\", len: %d}\n", src_ip_p, ntohs(hdr->src_port), dst_ip_p, ntohs(hdr->dst_port), ntohs(hdr->msg_len), msg_debug, *odr_msg_size);
}

void test_table(struct co_table * pt) {
    int cnt = 0;
    log_debug("!!!Test table!!!\n");
    log_debug("index\tport\tsun_path\tpermanent\n");
    while(pt != NULL) {
        log_debug("#%d\t%u\t%s\t%s\n", cnt++, pt->port, pt->sun_path, (pt->timer!=NULL)?"NO":"YES");
        pt = pt->next;
    }
}

void insert_table(struct co_table ** pt, uint16_t port, char * sun_path, void * timer) {
    struct co_table * cur = *pt;
    while(cur != NULL) {
        if(strcmp(cur->sun_path, sun_path) == 0) {
            log_warn("Table entry has already existed, port: %d, sun_path: %s.\n", port, sun_path);
            log_warn("Seems due to \'too short the timeout of sending request is\'?\n");
            return ;
        }
        cur = cur->next;
    }

    struct co_table * new_table = (struct co_table *) malloc(sizeof(struct co_table));
    new_table->port = port;
    strcpy(new_table->sun_path, sun_path);
    new_table->timer = timer;
    new_table->next = *pt;
    *pt = new_table;

    test_table(table_head);
}

struct co_table * search_table_by_port(struct co_table * pt, uint16_t port) {
    while(pt != NULL) {
        if(port == pt->port) {
            log_debug("found! %u: %s, %16x\n", pt->port, pt->sun_path, pt->timer);
            return pt;
        }
        pt = pt->next;
    }
    return NULL;
}

struct co_table * search_table_by_sun_path(struct co_table * pt, char * sun_path) {
    if(sun_path == NULL) return NULL;
    while(pt != NULL) {
        if(strcmp(sun_path, pt->sun_path) == 0) {
            return pt;
        }
        pt = pt->next;
    }
    return NULL;
}

// remove from table when timeout
void remove_from_table_by_port(struct co_table ** pt, uint16_t port) {
    struct co_table * prev = NULL, * head = *pt;
    while(head != NULL) {
        if(head->port == port) {
            log_warn("Entry with port#%u, sun_path \"%s\" gonna be removed, due to expiration!\n", head->port, head->sun_path);
            if(prev != NULL) {
                prev->next = head->next;
            } else {
                *pt = head->next;
            }
            free(head);
            return ;
        }

        prev = head;
        head = head->next;
    }

    log_err("Entry gonna be removed, with port#%u not found! Maybe something wrong!\n", port);
}

void destroy_table(struct co_table ** pt) {
    log_debug("Destorying the mapping table ...\n");
    struct co_table * next = NULL;
    while(*pt != NULL) {
        next = (*pt)->next;
        free(*pt);
        (*pt) = next;
    }
}

void entry_timeout(void * ml, void * data, const struct timeval * elapse) {

    uint16_t port = *((uint16_t *)data);
    remove_from_table_by_port(&table_head, port);
    log_debug("expired port number: %u\n", port);
    test_table(table_head);
}

void data_callback(void * buf, uint16_t len, void * data) {
    // haven't been tested
    log_debug("gonna push message back to application layer!\n");
    uint8_t payload[MSG_MAX_LEN], send_dgram[DGRAM_MAX_LEN];
    struct sockaddr_un tar_addr;
    struct odr_msg_hdr o_hdr;
    struct recv_msg_hdr * r_hdr = NULL;
    int sent_size = 0;
    socklen_t tar_len = 0;
    struct co_table * table_entry = NULL;
    struct bound_odr * b_o = data;

    int sockfd = *(int *)(b_o->fd_p);
    void * ml = b_o->ml;

    // parse odr_msg
    memcpy(&o_hdr, buf, sizeof(struct odr_msg_hdr));
    memcpy(payload, buf + sizeof(struct odr_msg_hdr), len - sizeof(struct odr_msg_hdr));

    // make msg to push back to application layer
    make_recv_msg(send_dgram, make_recv_hdr(r_hdr, inet_ntoa((struct in_addr){o_hdr.src_ip}), ntohs(o_hdr.src_port), ntohs(o_hdr.msg_len)), payload, o_hdr.msg_len, &sent_size);

    // find port
    uint16_t port = (ntohs(o_hdr.dst_port));
    table_entry = search_table_by_port(table_head, port);
    if(table_entry == NULL) {
        if(port == TIM_SERV_PORT) {
            log_err("Time server port #%u is not open; time server is not running currently!\n", TIM_SERV_PORT);
        } else {
            log_err("The table entry (%u, %s) has expired!\n", port, inet_ntoa((struct in_addr){o_hdr.src_ip}));
        }
        return ;
    } else {
        // if server port, then no need to deal with timer, as it's the permanent entry and has no timer
        if(port != TIM_SERV_PORT) {
            // re-init timer for that non-permanent entry
            timer_remove(ml, table_entry->timer);
            struct timeval tv;
            tv.tv_sec = TIM_LIV_NON_PERMAN;
            tv.tv_usec = 0;
            timer_insert(ml, &tv, entry_timeout, &port);
        }
    }
    log_err("gogogo\n");

    log_debug("gonna send message to table_entry with port#%u, sun_path [%s]\n", table_entry->port, table_entry->sun_path);
    memset(&tar_addr, 0, sizeof(struct sockaddr_un));
    tar_addr.sun_family = AF_LOCAL;
    strcpy(tar_addr.sun_path, table_entry->sun_path);

    tar_len = sizeof(tar_addr);
    // should get un fd first
    if((sent_size = sendto(sockfd, send_dgram, sent_size, 0, (struct sockaddr *) &tar_addr, tar_len)) < 0) {
        log_err("sendto error\n");
        return ;
    }
    log_debug("msg forwarded! data callback done!\n");
}

void client_callback(void * ml, void * data, int rw) {

    struct bound_app * b_a = data;
    struct fd * fh = b_a->fh;
    struct odr_protocol * op = b_a->op;
    int sockfd = fd_get_fd(fh);
    int recv_size = 0;
    uint8_t sent_msg[DGRAM_MAX_LEN], sent_payload[MSG_MAX_LEN], odr_msg[ODR_MSG_MAX_LEN];
    struct sockaddr_un cli_addr;
    socklen_t cli_len = sizeof(struct sockaddr_un);
    struct send_msg_hdr s_hdr;
    struct odr_msg_hdr * o_hdr = NULL;
    uint32_t src_ip;
    uint16_t src_port;
    int payload_len = 0, odr_msg_len;

    // recv the time client request
    if((recv_size = recvfrom(sockfd, sent_msg, (size_t) DGRAM_MAX_LEN, 0, (struct sockaddr *) &cli_addr, &cli_len)) < 0) {
        my_err_quit("recvfrom error");
    }

    log_debug("Message retrieved from application layer!\n");

    // send ODR message, TODO
    // construct ODR msg hdr
    // using time client request to get dst ip and port
    // along with local ip and random port
    struct ifi_info * head = get_ifi_info(AF_INET, 0);
    while(head) {
        if(strcmp(head->ifi_name, "eth0") == 0) {
            struct sockaddr_in *s = (struct sockaddr_in *) head->ifi_addr;
            src_ip = s->sin_addr.s_addr;
            break;
        }
        head = head->ifi_next;
    }

    if(search_table_by_sun_path(table_head, cli_addr.sun_path) != NULL) {
        log_warn("Time client with sun_path \"%s\" is already in mapping table! No need to generate random port!\n", cli_addr.sun_path);
        goto SEND_ODR_MSG;
    }

GEN_RAND_PORT:

    src_port = rand() % MAX_PORT_NUM;
    // a stupid method to avoid duplicate port number
    struct co_table * cur = table_head;
    while(cur) {
        if(cur->port == src_port) {
            log_debug("Poor you! How back luck to generate a conflict port number!\n");
            goto GEN_RAND_PORT;
        }
        cur = cur->next;
    }

    if(src_port == TIM_SERV_PORT) {
        log_debug("Poor you! How back luck to generate a conflict port number!\n");
        goto GEN_RAND_PORT;
    }

    // add timer for non-permanent entry
    struct timeval tv;
    tv.tv_sec = TIM_LIV_NON_PERMAN;
    tv.tv_usec = 0;

    // insert new non-permanent client sun_path and corresponding port
    insert_table(&table_head, src_port, cli_addr.sun_path, timer_insert(ml, &tv, entry_timeout, &src_port));

SEND_ODR_MSG:
    payload_len = recv_size - sizeof(struct send_msg_hdr);
    // parse application message
    memcpy(&s_hdr, sent_msg, sizeof(struct send_msg_hdr));
    memcpy(sent_payload, sent_msg + sizeof(struct send_msg_hdr), payload_len);
    make_odr_msg(odr_msg,
            make_odr_msg_hdr(o_hdr, src_ip, htons(src_port), s_hdr.dst_ip, s_hdr.dst_port, htons(payload_len)),
            sent_payload,
            payload_len,
            &odr_msg_len);

    log_debug("going to send message via odr!\n");

    // send msg via odr
    send_msg_api(op, s_hdr.dst_ip, odr_msg, odr_msg_len, s_hdr.flag);
}

int main(int argc, const char **argv) {

    char local_host_name[HOST_NAME_MAX_LEN];
    int sock_un_fd;
    int path_len;
    socklen_t sock_len = 0;
    struct sockaddr_un odr_addr, odr_addr_info;
    int staleness = 0;
    if(argc < 2) {
        log_err("Number of parameters incorrect!\n");
        log_warn("Usage: ./odr_server <stale> #in seconds\n");
        exit(EXIT_FAILURE);
    } else {
        staleness = atoi(argv[1]);
        log_info("staleness: %d second(s) (%d milliseconds)\n", staleness, staleness * 1000);
        staleness *= 1000;
    }

    // init srand for random port
    srand(time(NULL));
    // table: port<->sun_path
    table_head = NULL; // init
    log_debug("Time server, info: %u, sun_path: %s\n", TIM_SERV_PORT, ODR_SUN_PATH);
    insert_table(&table_head, TIM_SERV_PORT, TIM_SERV_SUN_PATH, NULL);
    // just test
    // destroy_table(&table_head);
    // test_table(table_head);

    if(gethostname(local_host_name, sizeof(local_host_name)) < 0) {
        my_err_quit("gethostname error");
    }

    log_info("Current node: <%s>\n", local_host_name);
    log_info("ODR Process started and gonna create UNIX domain datagram socket!\n");

    unlink(ODR_SUN_PATH);
    path_len = strlen(ODR_SUN_PATH);

    // create unix domain socket
    if((sock_un_fd = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0) {
        unlink(ODR_SUN_PATH); // we should manually collect junk, maybe marked as TODO
        my_err_quit("socket error");
    }

    // init odr addr
    memset(&odr_addr, 0, sizeof(odr_addr));
    odr_addr.sun_family = AF_LOCAL;
    strncpy(odr_addr.sun_path, ODR_SUN_PATH, path_len);
    odr_addr.sun_path[path_len] = 0;

    // bind
    if(bind(sock_un_fd, (struct sockaddr *) &odr_addr, sizeof(odr_addr)) < 0) {
        unlink(ODR_SUN_PATH); // we should manually collect junk, maybe marked as TODO
        my_err_quit("bind error");
    }

    if (chmod(ODR_SUN_PATH, 0777) < 0) {
        unlink(ODR_SUN_PATH); // we should manually collect junk, maybe marked as TODO
        my_err_quit("chmod error");
    }

    // after binding, get sock info
    sock_len = sizeof(odr_addr_info);
    if(getsockname(sock_un_fd, (struct sockaddr *) &odr_addr_info, &sock_len) < 0) {
        unlink(ODR_SUN_PATH); // we should manually collect junk, maybe marked as TODO
        my_err_quit("getsockname error");
    }

    log_debug("ODR Process unix domain socket created, socket sun path: %s, socket structure size: %u\n", odr_addr_info.sun_path, (unsigned int) sock_len);

    void * ml = mainloop_new();

    struct bound_odr * b_o = (struct bound_odr *) malloc(sizeof(struct bound_odr));
    b_o->fd_p = &sock_un_fd;
    b_o->ml = ml;
    // init odr 
    struct odr_protocol * op = odr_protocol_init(ml, data_callback, b_o, staleness, get_ifi_info(AF_INET, 0));

    // set application response
    void * fh = fd_insert(ml, sock_un_fd, FD_READ, client_callback, NULL);
    struct bound_app * b_a = (struct bound_app *) malloc(sizeof(struct bound_app));
    b_a->fh = fh;
    b_a->op = op;
    fd_set_data(fh, b_a);

    mainloop_run(ml);

    log_info("All work done!\n");
    log_info("Cleaning resources ...\n");
    free(ml);
    destroy_table(&table_head);
    log_info("ODR Process quiting!\n");

	return 0;
}
