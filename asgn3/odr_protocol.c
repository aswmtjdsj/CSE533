#include <time.h>

#include "odr_hdr.h"
#include "mainloop.h"
#include "odr_protocol.h"
#include "skiplist.h"
#include "utils.h"
#include "log.h"
struct msg {
	uint16_t flags;
	uint64_t stale;
	uint16_t len;
	uint8_t *buf;
	struct msg *next;
};
struct odr_protocol {
	int fd;
	int max_idx;
	void *buf;
	size_t buf_len;
	void *fh;
	data_cb cb;
	uint32_t myip;
	struct skip_list_head *route_table;
	struct skip_list_head *known_hosts;
	struct ifi_info *ifi_table;
	//messages held back because there's no route
	struct msg *pending_msgs;
};
struct host_entry {
	struct skip_list_head h;
	uint32_t ip;
	uint64_t last_broadcast_id;
};
struct route_entry {
	struct skip_list_head h;
	uint32_t dst_ip;
	int halen;
	char route_mac[8];
	uint64_t timestamp;
	uint32_t hop_count;
};
int addr_cmp(struct skip_list_head *h, const void *b) {
	struct route_entry *re = skip_list_entry(h, struct route_entry, h);
	const int *bb = b;
	return re->dst_ip-(*bb);
}
static inline uint64_t
get_timestamp(void) {
	struct timespec ts;
	int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0) {
		log_warning("clock_gettime failed !!! %s\n",
		    strerror(errno));
		exit(1);
	}

	uint64_t t = (uint64_t)ts.tv_sec*(uint64_t)1000;
	t += (uint64_t)(ts.tv_nsec/1000000);
	return t;
}
static inline int
send_msg(struct odr_protocol *op, const struct msg *msg) {
	return 0;
}
static inline void
route_table_update(struct odr_protocol *op, struct sockaddr_ll *addr) {
	//Update route table from information in the packet
	struct odr_hdr *hdr = op->buf;
	struct skip_list_head *res = skip_list_find_le(op->route_table,
	    &hdr->saddr, addr_cmp);
	struct route_entry *re = skip_list_entry(res, struct route_entry, h);
	if (!res || re->dst_ip != hdr->daddr) {
		re = calloc(1, sizeof(struct route_entry));
		re->dst_ip = hdr->saddr;
		re->hop_count = hdr->hop_count;
		re->halen = addr->sll_halen;
		memcpy(re->route_mac, addr->sll_addr, addr->sll_halen);
		re->timestamp = get_timestamp();
		skip_list_insert(op->route_table, &re->h,
		    &re->dst_ip, addr_cmp);
	} else {
		if (re->hop_count > hdr->hop_count) {
			re->halen = addr->sll_halen;
			re->hop_count = hdr->hop_count;
			memcpy(re->route_mac, addr->sll_addr, re->halen);
			re->timestamp = get_timestamp();
		} else if (re->hop_count == hdr->hop_count)
			re->timestamp = get_timestamp();
	}

	//Then check op->pending_msgs to send out all message we can send
	struct msg *tmp = op->pending_msgs;
	struct msg **nextp = &op->pending_msgs;
	while(tmp) {
		int ret = send_msg(op, tmp);
		if (ret) {
			//Succeeded
			*nextp = tmp->next;
			free(tmp->buf);
			free(tmp);
			tmp = *nextp;
		} else {
			//Failed
			nextp = &tmp->next;
			tmp = tmp->next;
		}
	}
}
static inline void
rreq_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
	struct odr_hdr *hdr = op->buf;
	if (hdr->daddr == op->myip) {
		//We are the target
	}
	struct skip_list_head *res =
	    skip_list_find_le(op->route_table, &hdr->daddr, addr_cmp);
	struct route_entry *re = skip_list_entry(res, struct route_entry, h);
	if (!res || re->dst_ip != hdr->daddr) {
		//Not found
	} else {
	}
}
static inline void
rrep_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
}
static inline void
data_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
}

static inline void
enlarge_buffer(struct odr_protocol *op, size_t s) {
	if (s <= op->buf_len)
		return;
	op->buf = realloc(op->buf, s);
	op->buf_len = s;
}

void odr_read_cb(void *ml, void *data, int rw){
	struct odr_protocol *op = data;
	int ret;
	struct sockaddr_ll addr;
	socklen_t len = sizeof(addr);
	ret = recvfrom(op->fd, op->buf, 0, MSG_PEEK|MSG_TRUNC,
	    (struct sockaddr *)&addr, &len);

	if (ret < 0) {
		log_warning("Failed to recvfrom(), %s\n",
		    strerror(errno));
		return;
	}

	int mtu = op->ifi_table[addr.sll_ifindex].ifi_mtu;
	log_info("Packet coming in from %d\n", addr.sll_ifindex);
	if (ret > mtu && mtu)
		log_warning("Packet larger than mtu (%d>%d)\n",
		    ret, mtu);
	enlarge_buffer(op, ret);
	ret = recvfrom(op->fd, op->buf, op->buf_len, 0,
	    (struct sockaddr *)&addr, &len);
	log_info("Packet coming in from %d\n", addr.sll_ifindex);

	struct odr_hdr *hdr = op->buf;
	if (hdr->flags & ODR_RREQ)
		rreq_handler(op, &addr);
	if (hdr->flags & ODR_RREP)
		rrep_handler(op, &addr);
	if (hdr->flags & ODR_DATA)
		data_handler(op, &addr);
}
void odr_protocol_init(void *ml, data_cb cb, struct ifi_info *head) {
	int sockfd = socket(AF_PACKET, SOCK_DGRAM, htons(ODR_MAGIC));
	struct odr_protocol *op = malloc(sizeof(struct odr_protocol));
	op->fd = sockfd;
	op->fh = fd_insert(ml, sockfd, FD_READ, odr_read_cb, op);
	op->buf = NULL;
	op->cb = cb;
	op->buf_len = 0;

	struct ifi_info *tmp = head;
	int max_idx = 0;
	while(tmp) {
		if (tmp->ifi_index > max_idx)
			max_idx = tmp->ifi_index;
		tmp = tmp->ifi_next;
	}
	op->ifi_table = calloc(max_idx, sizeof(struct ifi_info));
	tmp = head;
	while(tmp) {
		if (op->ifi_table[tmp->ifi_index].ifi_name[0])
			log_warning("Duplicated interface, shouldn't use"
			    " doalias\n");
		if (strcmp(tmp->ifi_name, "eth0")) {
			log_info("Ignoring eth0 (but recording the ip)\n");
			struct sockaddr_in *s = (struct sockaddr_in *)
			    tmp->ifi_addr;
			op->myip = s->sin_addr.s_addr;
			continue;
		}
		if (tmp->ifi_flags & IFF_LOOPBACK) {
			log_info("Ignoring loopback interface\n");
			continue;
		}
		memcpy(op->ifi_table+tmp->ifi_index, tmp,
		       sizeof(struct ifi_info));
	}
}
