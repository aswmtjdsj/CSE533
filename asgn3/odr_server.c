#include <time.h>
#include <sys/time.h>

#include "log.h"
#include "mainloop.h"
#include "const.h"
#include "odr_protocol.h"

struct co_table {
    int port;
    char sun_path[SUN_PATH_MAX_LEN];
	//struct timeval tv;
	//struct timeval remain_tv;
    int time_to_live; // ms?
    struct co_table * next;
};

struct co_table * table_head;

void test_table(struct co_table * pt) {
    int cnt = 0;
    log_debug("index\tport\tsun_path\ttime_to_live\n");
    while(pt != NULL) {
        log_debug("#%d\t%d\t%s\t%d\n", cnt++, pt->port, pt->sun_path, pt->time_to_live);
        pt = pt->next;
    }
}

void insert_table(struct co_table ** pt, int port, char * sun_path, int time_to_live) {
    struct co_table * prev = *pt, *cur = *pt;
    while(cur != NULL) {
        prev = cur;
        cur = cur->next;
    }
    if(prev == NULL) {
        *pt = (struct co_table *) malloc(sizeof(struct co_table));
        (*pt)->port = port;
        strcpy((*pt)->sun_path, sun_path);
        (*pt)->time_to_live = time_to_live;
        (*pt)->next = NULL;
    } else {
        prev->next = malloc(sizeof(struct co_table));
        cur = prev->next;
        cur->port = port;
        strcpy(cur->sun_path, sun_path);
        cur->time_to_live = time_to_live;
        cur->next = NULL;
    }
    log_debug("table entry inserted!\n");
    test_table(*pt);
}

char * search_table(struct co_table * pt, int port) {
    while(pt != NULL) {
        if(port == pt->port) {
            return pt->sun_path;
        }
        pt = pt->next;
    }
    return NULL;
}

void data_callback(void *buf, uint16_t len) {

}

void client_callback(void * ml, void * data, int rw) {

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

    // table: port<->sun_path
    table_head = NULL; // init
    log_debug("Time server, info: %d, sun_path: %s\n", TIM_SERV_PORT, TIM_SERV_SUN_PATH);
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

    void * fh = fd_insert(ml, sock_un_fd, FD_READ, client_callback, NULL);

    struct odr_protocol * op = odr_protocol_init(ml, data_callback, STALE_SEC, get_ifi_info(AF_INET, 0));

    mainloop_run(ml);

    log_info("ODR Process quiting!\n");
    free(ml);

	return 0;
}
