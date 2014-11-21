#include <time.h>
#include <sys/time.h>

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
    int time_to_live; // ms?
    struct co_table * next;
};

struct odr_msg_hdr {
    uint32_t src_ip;
    uint16_t src_port;
    uint32_t dst_ip;
    uint16_t dst_port;
    uint16_t msg_len;
};

struct bound_data {
    void * fh;
    void * op;
};

struct co_table * table_head;

struct odr_msg_hdr * make_odr_msg_hdr(struct odr_msg_hdr * hdr, uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port, int len) {

    if(hdr == NULL) {
        hdr = malloc(sizeof(struct odr_msg_hdr));
    }

    hdr->src_ip = src_ip;
    hdr->src_port = htons(src_port);
    hdr->dst_ip = dst_ip;
    hdr->dst_port = htons(dst_port);
    hdr->msg_len = htons(len);

    return hdr;
}

void make_odr_msg(uint8_t * odr_msg, struct odr_msg_hdr * hdr, void * payload, int payload_len, int * odr_msg_size) {
    memcpy(odr_msg, hdr, sizeof(struct odr_msg_hdr));
    memcpy(odr_msg + sizeof(struct odr_msg_hdr), payload, payload_len);
    *odr_msg_size = sizeof(struct odr_msg_hdr) + payload_len;
    log_debug("ODR message size: %d\n", *odr_msg_size);
    log_debug("ODR message payload: %s\n", (char *)payload);
}

void test_table(struct co_table * pt) {
    int cnt = 0;
    log_debug("index\tport\tsun_path\ttime_to_live\n");
    while(pt != NULL) {
        log_debug("#%d\t%u\t%s\t%d\n", cnt++, pt->port, pt->sun_path, pt->time_to_live);
        pt = pt->next;
    }
}

void insert_table(struct co_table ** pt, uint16_t port, char * sun_path, int time_to_live) {
    struct co_table * cur = *pt;
    while(cur != NULL) {
        if(strcmp(cur->sun_path, sun_path) == 0) {
            log_warn("Table entry has already existed, port: %d, sun_path: %s.\n", port, sun_path);
            log_warn("Seems due to \'too short the timeout of sending request is\'?\n");
            return ;
        }
    }

    struct co_table * new_table = (struct co_table *) malloc(sizeof(struct co_table));
    new_table->port = port;
    strcpy(new_table->sun_path, sun_path);
    new_table->time_to_live = time_to_live;
    new_table->next = *pt;
    *pt = new_table;

    test_table(table_head);
}

char * search_table(struct co_table * pt, uint16_t port) {
    while(pt != NULL) {
        if(port == pt->port) {
            return pt->sun_path;
        }
        pt = pt->next;
    }
    return NULL;
}

// remove from table when timeout
void remove_from_table(struct co_table * pt) {
}

void destroy_table(struct co_table * pt) {
    log_debug("Destorying the mapping table ...\n");
    struct co_table * next = NULL;
    while(pt != NULL) {
        next = pt->next;
        free(pt);
        pt = next;
    }
}

void data_callback(void * buf, uint16_t len, void * data) {
    log_debug("gonna push message back to application layer!\n");
    uint8_t payload[MSG_MAX_LEN], send_dgram[DGRAM_MAX_LEN];
    struct sockaddr_un tar_addr;
    struct odr_msg_hdr o_hdr;
    struct recv_msg_hdr * r_hdr = NULL;
    int sent_size = 0;
    socklen_t tar_len = 0;
    char * sun_path = NULL;
    int sockfd = *(int *)data;

    // parse odr_msg
    memcpy(&o_hdr, buf, sizeof(struct odr_msg_hdr));
    memcpy(payload, buf + sizeof(struct odr_msg_hdr), len - sizeof(struct odr_msg_hdr));

    make_recv_hdr(r_hdr, inet_ntoa((struct in_addr){o_hdr.src_ip}), ntohs(o_hdr.src_port), ntohs(o_hdr.msg_len));
    memcpy(send_dgram, r_hdr, sizeof(struct send_msg_hdr));
    memcpy(send_dgram + sizeof(struct send_msg_hdr), payload, o_hdr.msg_len);
    sent_size = sizeof(struct send_msg_hdr) + o_hdr.msg_len;

    // find port
    sun_path = search_table(table_head, ntohs(o_hdr.dst_port));
    if(sun_path == NULL) {
        if(ntohs(o_hdr.dst_port) == TIM_SERV_PORT) {
            log_err("Time server port #%u is not open; time server is not running currently!\n");
        } else {
            log_err("The table entry (%u, %s) has expired!\n");
        }
        return ;
    }

    memset(&tar_addr, 0, sizeof(struct sockaddr_un));
    tar_addr.sun_family = AF_LOCAL;
    strcpy(tar_addr.sun_path, sun_path);

    tar_len = sizeof(tar_addr);
    // should get un fd first
    if((sent_size = sendto(sockfd, send_dgram, sent_size, 0, (struct sockaddr *) &tar_addr, tar_len)) < 0) {
        log_err("sendto error\n");
        return ;
    }
}

void client_callback(void * ml, void * data, int rw) {

    struct bound_data * b_d = data;
    struct fd * fh = b_d->fh;
    struct odr_protocol * op = b_d->op;
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
    
    src_port = rand() % MAX_PORT_NUM;
    payload_len = recv_size - sizeof(struct send_msg_hdr);
    memcpy(&s_hdr, sent_msg, sizeof(struct send_msg_hdr));
    memcpy(sent_payload, sent_msg + sizeof(struct send_msg_hdr), payload_len);
    make_odr_msg(odr_msg,
            make_odr_msg_hdr(o_hdr, src_ip, src_port, s_hdr.dst_ip, s_hdr.dst_port, payload_len),
            sent_payload,
            payload_len,
            &odr_msg_len);

    // call send message api
    send_msg_api(op, s_hdr.dst_ip, odr_msg, odr_msg_len, s_hdr.flag);

    // insert new non-permanent client sun_path and corresponding port
    insert_table(&table_head, src_port, cli_addr.sun_path, TIM_LIV_NON_PERMAN);
}

int main(int argc, const char **argv) {

	log_server_init(argc, argv);
	// log_debug("Test log\n");

    char local_host_name[HOST_NAME_MAX_LEN];
    int sock_un_fd;
    char odr_proc_sun_path[SUN_PATH_MAX_LEN] = ODR_SUN_PATH;
    int path_len;
    socklen_t sock_len = 0;
    struct sockaddr_un odr_addr, odr_addr_info;

    // init srand for random port
    srand(time(NULL));
    // table: port<->sun_path
    table_head = NULL; // init
    log_debug("ODR server, info: %d, sun_path: %s\n", TIM_SERV_PORT, TIM_SERV_SUN_PATH);
    insert_table(&table_head, TIM_SERV_PORT, TIM_SERV_SUN_PATH, TIM_LIV_PERMAN);

    if(gethostname(local_host_name, sizeof(local_host_name)) < 0) {
        my_err_quit("gethostname error");
    }

    log_info("Current node: <%s>\n", local_host_name);
    log_info("ODR Process started and gonna create UNIX domain datagram socket!\n");

    unlink(odr_proc_sun_path);
    path_len = strlen(odr_proc_sun_path);

    // create unix domain socket
    if((sock_un_fd = socket(AF_LOCAL, SOCK_DGRAM, 0)) < 0) {
        unlink(odr_proc_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("socket error");
    }

    // init odr addr
    memset(&odr_addr, 0, sizeof(odr_addr));
    odr_addr.sun_family = AF_LOCAL;
    strncpy(odr_addr.sun_path, odr_proc_sun_path, path_len);
    odr_addr.sun_path[path_len] = 0;

    // bind
    if(bind(sock_un_fd, (struct sockaddr *) &odr_addr, sizeof(odr_addr)) < 0) {
        unlink(odr_proc_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("bind error");
    }

    // after binding, get sock info
    sock_len = sizeof(odr_addr_info);
    if(getsockname(sock_un_fd, (struct sockaddr *) &odr_addr_info, &sock_len) < 0) {
        unlink(odr_proc_sun_path); // we should manually collect junk, maybe marked as TODO
        my_err_quit("getsockname error");
    }

    log_debug("ODR Process unix domain socket created, socket sun path: %s, socket structure size: %u\n", odr_addr_info.sun_path, (unsigned int) sock_len);

    void * ml = mainloop_new();

    // init odr 
    struct odr_protocol * op = odr_protocol_init(ml, data_callback, &sock_un_fd, STALE_SEC, get_ifi_info(AF_INET, 0));

    // set application response
    void * fh = fd_insert(ml, sock_un_fd, FD_READ, client_callback, NULL);
    struct bound_data * b_d = (struct bound_data *) malloc(sizeof(struct bound_data));
    b_d->fh = fh;
    b_d->op = op;
    fd_set_data(fh, b_d);

    mainloop_run(ml);

    log_info("ODR Process quiting!\n");
    free(ml);

	return 0;
}
