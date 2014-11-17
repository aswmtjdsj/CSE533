#include <time.h>

#include "odr_hdr.h"
#include "mainloop.h"
#include "odr_protocol.h"
#include "skiplist.h"
#include "utils.h"
#include "log.h"
struct odr_protocol {
	int fd;
	int max_idx;
	void *buf;
	size_t buf_len, msg_len;
	void *fh;
	data_cb cb;
	uint32_t myip;
	uint64_t bid;
	uint64_t stale;
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
	int ifi_idx;
	uint16_t halen;
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
		log_warn("clock_gettime failed !!! %s\n",
		    strerror(errno));
		exit(1);
	}

	uint64_t t = (uint64_t)ts.tv_sec*(uint64_t)1000;
	t += (uint64_t)(ts.tv_nsec/1000000);
	return t;
}
static inline int
send_msg_dontqueue(struct odr_protocol *op, const struct msg *msg,
		   int direct) {
	struct odr_hdr *hdr = msg->buf;
	struct skip_list_head *res = skip_list_find_le(op->route_table,
	    &hdr->daddr, addr_cmp);
	if (!res)
		//No route to host
		return 0;

	struct route_entry *re = skip_list_entry(res, struct route_entry, h);
	uint64_t now = get_timestamp();
	if ((msg->flags & ODR_MSG_STALE) && !direct)
		//Force rediscovery
		return 0;

	if (re->timestamp+op->stale < now)
		//Route stale
		return 0;

	//Send it out!
	struct sockaddr_ll lladdr;
	memset(&lladdr, 0, sizeof(lladdr));
	lladdr.sll_ifindex = re->ifi_idx;
	memcpy(lladdr.sll_addr, re->route_mac, re->halen);
	lladdr.sll_halen = re->halen;
	lladdr.sll_family = AF_PACKET;
	lladdr.sll_protocol = htons(ODR_MAGIC);

	int ret = sendto(op->fd, msg->buf, msg->len, 0,
	    (struct sockaddr *)&lladdr, sizeof(lladdr));
	if (ret < 0) {
		log_warn("Failed to send packet\n");
		return 0;
	}
	return 1;
}
int send_msg(struct odr_protocol *op, struct msg *msg) {
	int ret = send_msg_dontqueue(op, msg, 0);
	if (ret)
		return ret;

	//Send failed, add to queue
	msg->next = op->pending_msgs;
	op->pending_msgs = msg;
	return 0;
}
static inline void
route_table_update(struct odr_protocol *op, uint32_t daddr,
		   uint32_t hop_count, struct sockaddr_ll *addr) {
	//Update route table from information in the packet
	struct odr_hdr *hdr = op->buf;
	struct skip_list_head *res = skip_list_find_le(op->route_table,
	    &daddr, addr_cmp);
	struct route_entry *re = skip_list_entry(res, struct route_entry, h);
	int send_radv = 1;
	if (!res || re->dst_ip != daddr) {
		re = calloc(1, sizeof(struct route_entry));
		re->dst_ip = daddr;
		re->hop_count = hop_count;
		re->ifi_idx = addr->sll_ifindex;
		memcpy(re->route_mac, addr->sll_addr, addr->sll_halen);
		re->halen = addr->sll_halen;
		re->timestamp = get_timestamp();
		skip_list_insert(op->route_table, &re->h,
		    &re->dst_ip, addr_cmp);
	} else {
		if (re->hop_count > hop_count) {
			re->hop_count = hop_count;
			re->ifi_idx = addr->sll_ifindex;
			memcpy(re->route_mac, addr->sll_addr, addr->sll_halen);
			re->halen = addr->sll_halen;
			re->timestamp = get_timestamp();
		} else if (re->hop_count == hop_count)
			re->timestamp = get_timestamp();
		else
			send_radv = 0;
	}

	if (send_radv) {
		struct sockaddr_ll lladdr;
		lladdr.sll_family = AF_PACKET;
		lladdr.sll_protocol = htons(ODR_MAGIC);
		void *buf = malloc(sizeof(struct odr_hdr));
		struct odr_hdr *xhdr = buf;
		xhdr->flags = ODR_RADV;
		xhdr->hop_count = hop_count+1;
		xhdr->payload_len = 0;
		xhdr->bid = op->bid++;
		xhdr->daddr = 0;

		int i;
		for(i=0; i<op->max_idx; i++) {
			//Send RADV to all interfaces
			if (!op->ifi_table[i].ifi_flags & IFF_UP)
				continue;
			if (i == addr->sll_ifindex)
				continue;
			memcpy(lladdr.sll_addr,
			    op->ifi_table[i].ifi_hwaddr,
			    sizeof(struct sockaddr_ll));
			lladdr.sll_ifindex = i;
			lladdr.sll_halen = op->ifi_table[i].ifi_halen;
			int ret = sendto(op->fd, buf, sizeof(struct odr_hdr),
			    0, (struct sockaddr *)&lladdr, sizeof(lladdr));
			if (ret < 0)
				log_warn("Failed to send packet via %s",
				    op->ifi_table[i].ifi_name);
		}

		//Then check op->pending_msgs to send out all
		//message we can send
		struct msg *tmp = op->pending_msgs;
		struct msg **nextp = &op->pending_msgs;
		while(tmp) {
			struct odr_hdr *hdr = tmp->buf;
			if (hdr->daddr != daddr)
				//Not the route we updated
				continue;
			int ret = send_msg_dontqueue(op, tmp, 1);
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
}
static inline void
rreq_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
	struct odr_hdr *hdr = op->buf;
	route_table_update(op, hdr->saddr, hdr->hop_count, addr);
	if (hdr->bid > op->bid)
		op->bid = hdr->bid+1;

	if (hdr->daddr == op->myip) {
		//We are the target
		struct msg *nm = calloc(1, sizeof(struct msg));
		struct odr_hdr *xhdr = NULL;
		nm->buf = calloc(1, sizeof(struct odr_hdr));
		xhdr = nm->buf;
		xhdr->flags = ODR_RREP;
		xhdr->saddr = op->myip;
		xhdr->daddr = hdr->saddr;
		xhdr->hop_count = 0;
		nm->len = sizeof(struct odr_hdr);

		int ret = send_msg_dontqueue(op, nm, 0);
		if (!ret)
			log_warn("Can't find route for RREP, IMPOSSIBLE\n");
		return;
	}
	struct skip_list_head *res =
	    skip_list_find_le(op->route_table, &hdr->daddr, addr_cmp);
	struct route_entry *re = skip_list_entry(res, struct route_entry, h);
	if (!res || re->dst_ip != hdr->daddr) {
		//Not found
		int i;
		struct sockaddr_ll lladdr;
		hdr->hop_count++;
		for(i=0; i<op->max_idx; i++) {
			//Broadcast to all interfaces
			if (!op->ifi_table[i].ifi_flags & IFF_UP)
				continue;
			if (i == addr->sll_ifindex)
				continue;
			memcpy(lladdr.sll_addr,
			    op->ifi_table[i].ifi_hwaddr,
			    sizeof(struct sockaddr_ll));
			lladdr.sll_ifindex = i;
			lladdr.sll_halen = op->ifi_table[i].ifi_halen;
			int ret = sendto(op->fd, op->buf, op->msg_len,
			    0, (struct sockaddr *)&lladdr, sizeof(lladdr));
			if (ret < 0)
				log_warn("Failed to send packet via %s: %s",
				   op->ifi_table[i].ifi_name, strerror(errno));
		}
	} else {
		struct msg *nm = calloc(1, sizeof(struct msg));
		struct odr_hdr *xhdr = NULL;
		nm->buf = calloc(1, sizeof(struct odr_hdr));
		xhdr = nm->buf;
		xhdr->daddr = hdr->saddr;
		xhdr->saddr = hdr->daddr;
		xhdr->hop_count = re->hop_count;
		xhdr->flags = ODR_RREP;
		nm->len = sizeof(struct odr_hdr);

		int ret = send_msg_dontqueue(op, nm, 0);
		if (!ret)
			log_warn("Can't find route for RREP, IMPOSSIBLE\n");
		return;
	}
}
static inline void
rrep_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
	struct odr_hdr *hdr = op->buf;
	route_table_update(op, hdr->saddr, hdr->hop_count, addr);

	if (hdr->daddr != op->myip) {
		//Route the RREP
		hdr->hop_count++;
		struct msg *msg = calloc(1, sizeof(struct msg));
		msg->len = op->msg_len;
		msg->flags = 0;
		msg->buf = malloc(msg->len);
		memcpy(msg->buf, op->buf, msg->len);
		//Send, possibly queueing it
		send_msg(op, msg);
	}
}
static inline void
data_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
	struct odr_hdr *hdr = op->buf;
	route_table_update(op, hdr->saddr, hdr->hop_count, addr);

	if (hdr->daddr != op->myip) {
		//Route the packet
		hdr->hop_count++;
		struct msg *msg = calloc(1, sizeof(struct msg));
		msg->len = op->msg_len;
		msg->flags = 0;
		msg->buf = malloc(msg->len);
		memcpy(msg->buf, op->buf, msg->len);
		//Send, possibly queueing it
		send_msg(op, msg);
	} else
		//Deliver the packet
		op->cb((void *)(hdr+1));
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
		log_warn("Failed to recvfrom(), %s\n",
		    strerror(errno));
		return;
	}

	int mtu = op->ifi_table[addr.sll_ifindex].ifi_mtu;
	log_info("Packet coming in from %d\n", addr.sll_ifindex);
	if (ret > mtu && mtu)
		log_warn("Packet larger than mtu (%d>%d)\n",
		    ret, mtu);
	enlarge_buffer(op, ret);
	ret = recvfrom(op->fd, op->buf, op->buf_len, 0,
	    (struct sockaddr *)&addr, &len);
	op->msg_len = ret;
	log_info("Packet coming in from %d\n", addr.sll_ifindex);

	struct odr_hdr *hdr = op->buf;
	if (hdr->flags & ODR_RREQ)
		rreq_handler(op, &addr);
	if (hdr->flags & ODR_RREP)
		rrep_handler(op, &addr);
	if (hdr->flags & ODR_DATA)
		data_handler(op, &addr);
}
void odr_protocol_init(void *ml, data_cb cb, int stale,
		       struct ifi_info *head) {
	int sockfd = socket(AF_PACKET, SOCK_DGRAM, htons(ODR_MAGIC));
	struct odr_protocol *op = malloc(sizeof(struct odr_protocol));
	op->fd = sockfd;
	op->fh = fd_insert(ml, sockfd, FD_READ, odr_read_cb, op);
	op->buf = NULL;
	op->cb = cb;
	op->buf_len = 0;
	op->stale = stale;

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
			log_warn("Duplicated interface, shouldn't use"
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
		if (tmp->ifi_flags & IFF_UP) {
			log_info("Interface %s not up\n", tmp->ifi_name);
			continue;
		}
		memcpy(op->ifi_table+tmp->ifi_index, tmp,
		       sizeof(struct ifi_info));
	}
}
