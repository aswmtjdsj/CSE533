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
	void *cbdata;
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
	uint8_t route_mac[8];
	uint64_t timestamp;
	uint32_t hop_count;
};
static int addr_cmp(struct skip_list_head *h, const void *b) {
	struct route_entry *re = skip_list_entry(h, struct route_entry, h);
	const uint32_t *bb = b;
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
	if (hdr->daddr == op->myip) {
		//Send to local, deliever it directly
		op->cb((void *)(hdr+1), ntohs(hdr->payload_len), hdr->saddr,
		    op->cbdata);
		return 1;
	}
	struct skip_list_head *res = skip_list_find_le(op->route_table,
	    &hdr->daddr, addr_cmp);
	struct route_entry *re = skip_list_entry(res, struct route_entry, h);
	if (!res || re->dst_ip != hdr->daddr) {
		//No route to host
		log_info("Route to destination %s not found\n",
		    inet_ntoa((struct in_addr){hdr->daddr}));
		return 0;
	}

	uint64_t now = get_timestamp();
	if (msg->flags && !direct) {
		log_info("Route to destination %s found, but flag is set\n",
		    inet_ntoa((struct in_addr){hdr->daddr}));
		//Force rediscovery
		return 0;
	}

	if (re->timestamp+op->stale < now) {
		log_info("Route to destination %s found, but staled\n",
		    inet_ntoa((struct in_addr){hdr->daddr}));
		//Route stale
		return 0;
	}

	//Send it out!
	struct sockaddr_ll lladdr;
	memset(&lladdr, 0, sizeof(lladdr));
	lladdr.sll_ifindex = re->ifi_idx;
	memcpy(lladdr.sll_addr, re->route_mac, re->halen);
	lladdr.sll_halen = re->halen;
	lladdr.sll_family = AF_PACKET;
	lladdr.sll_protocol = htons(ODR_MAGIC);
	lladdr.sll_pkttype = lladdr.sll_hatype = 0;

	int ret = sendto(op->fd, msg->buf, msg->len, 0,
	    (struct sockaddr *)&lladdr, sizeof(lladdr));
	if (ret < 0) {
		log_err("Failed to send packet\n");
		return 0;
	}
	return 1;
}
static inline void
dump_odr_hdr(struct odr_hdr *hdr) {
	log_debug("================ODR HEADER================\n");
	int flags = ntohs(hdr->flags);
	char tmpf[30] = "";
	char *tmpfp = tmpf;
	if (flags & ODR_DATA)
		tmpfp += sprintf(tmpfp, "DATA ");
	if (flags & ODR_RREP)
		tmpfp += sprintf(tmpfp, "RREP ");
	if (flags & ODR_RREQ)
		tmpfp += sprintf(tmpfp, "RREQ ");
	if (flags & ODR_RADV)
		tmpfp += sprintf(tmpfp, "RADV ");
	log_debug("\tFLAGS: %s\n", tmpf);
	log_debug("\tSource IP: %s\n",
	    inet_ntoa((struct in_addr){hdr->saddr}));
	log_debug("\tTarget IP: %s\n",
	    inet_ntoa((struct in_addr){hdr->daddr}));
	log_debug("\tHop count: %d\n", ntohs(hdr->hop_count));
	log_debug("\tBroadcast ID (only makes sense for RREQ): %d\n",
	    ntohl(hdr->bid));
}
static inline
int broadcast(struct odr_protocol *op, void *buf, size_t len, int exclude) {
	int i, c = 0;
	struct sockaddr_ll lladdr;
	char *tmp = NULL;
	size_t iplen = 0;
	lladdr.sll_protocol = htons(ODR_MAGIC);
	lladdr.sll_family = AF_PACKET;
	for(i=0; i<=op->max_idx; i++) {
		//Broadcast to all interfaces
		if (!(op->ifi_table[i].ifi_flags & IFF_UP))
			continue;
		if (i == exclude)
			continue;
		log_info("Send packet through %s (id: %d, ip: %s)\n",
		    op->ifi_table[i].ifi_name, i,
		    sa_ntop(op->ifi_table[i].ifi_addr, &tmp, &iplen));
		memset(lladdr.sll_addr, 255, IFHWADDRLEN);
		lladdr.sll_ifindex = i;
		lladdr.sll_halen = op->ifi_table[i].ifi_halen;
		lladdr.sll_hatype = lladdr.sll_pkttype = 0;
		int ret = sendto(op->fd, buf, len, 0,
		    (struct sockaddr *)&lladdr, sizeof(lladdr));
		if (ret < 0)
			log_warn("Failed to send packet via %s: %s\n",
			    op->ifi_table[i].ifi_name, strerror(errno));
		else
			c++;
	}
	free(tmp);
	return c;
}
static inline
int send_msg(struct odr_protocol *op, struct msg *msg) {
	int ret = send_msg_dontqueue(op, msg, 0);
	if (ret)
		return ret;

	//Send failed, add to queue
	msg->next = op->pending_msgs;
	op->pending_msgs = msg;

	//XXX And send rreq
	log_info("No route entry found, rediscovering...\n");
	void *buf = malloc(sizeof(struct odr_hdr));
	struct odr_hdr *xhdr = buf, *hdr = msg->buf;
	xhdr->daddr = hdr->daddr;
	xhdr->saddr = op->myip;
	xhdr->hop_count = 0;
	xhdr->flags = htons(ODR_RREQ);
	xhdr->bid = htonl(op->bid++);
	xhdr->payload_len = 0;

	broadcast(op, buf, sizeof(struct odr_hdr), -1);
	return 0;
}
int send_msg_api(struct odr_protocol *op, uint32_t dst_ip,
		 const void *buf, size_t len, int flags) {
	struct msg *msg = calloc(1, sizeof(struct msg));
	msg->flags = flags;
	msg->len = len+sizeof(struct odr_hdr);
	msg->buf = malloc(msg->len);

	struct odr_hdr *hdr = msg->buf;
	hdr->flags = htons(ODR_DATA);
	hdr->daddr = dst_ip;
	hdr->saddr = op->myip;
	hdr->bid = 0;
	hdr->payload_len = htons(len);
	hdr->hop_count = 0;
	memcpy((void *)(hdr+1), buf, len);

	return send_msg(op, msg);
}
//daddr and hop_count is in network order
static inline void
route_table_update(struct odr_protocol *op, uint32_t daddr,
		   uint32_t hop_count, struct sockaddr_ll *addr) {
	//Update route table from information in the packet
	if (daddr == op->myip)
		//No action needed
		return;
	struct skip_list_head *res = skip_list_find_le(op->route_table,
	    &daddr, addr_cmp);
	struct route_entry *re = skip_list_entry(res, struct route_entry, h);
	int send_radv = 1;
	hop_count = ntohs(hop_count);
	if (!res || re->dst_ip != daddr) {
		log_info("New route to %s through %s, hop: %d\n",
		    inet_ntoa((struct in_addr){daddr}),
		    mac_tostring(addr->sll_addr, addr->sll_halen),
		    hop_count);
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
			log_info("Update route to %s through %s, hop: %d"
			    " to through %s, hop: %d\n",
			    inet_ntoa((struct in_addr){daddr}),
			    mac_tostring(re->route_mac, re->halen),
			    re->hop_count,
			    mac_tostring(addr->sll_addr, addr->sll_halen),
			    hop_count);
			re->hop_count = hop_count;
			re->ifi_idx = addr->sll_ifindex;
			memcpy(re->route_mac, addr->sll_addr, addr->sll_halen);
			re->halen = addr->sll_halen;
			re->timestamp = get_timestamp();
		} else if (re->hop_count == hop_count) {
			log_info("Update timestamp on route to %s through %s,"
			    "hop: %d\n", inet_ntoa((struct in_addr){daddr}),
			    mac_tostring(re->route_mac, re->halen),
			    re->hop_count);
			re->timestamp = get_timestamp();
		} else
			send_radv = 0;
	}

	if (send_radv) {
		void *buf = malloc(sizeof(struct odr_hdr));
		struct odr_hdr *xhdr = buf;
		xhdr->flags = htons(ODR_RADV);
		xhdr->hop_count = htons(hop_count+1);
		xhdr->payload_len = 0;
		xhdr->bid = htonl(op->bid++);
		xhdr->daddr = 0;
		xhdr->saddr = daddr;

		log_info("Route updated, now we are advertising the new route"
		    " to our neighbours\n");

		broadcast(op, buf, sizeof(struct odr_hdr), addr->sll_ifindex);

		//Then check op->pending_msgs to send out all
		//message we can send
		log_info("Route updated, now checking if any pending messages"
		    " become sendable\n");
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
	if (ntohl(hdr->bid) > op->bid)
		op->bid = ntohl(hdr->bid)+1;

	//Looking up the source
	struct skip_list_head *hres = skip_list_find_le(op->known_hosts,
	    &hdr->saddr, addr_cmp);
	if (!hres) {
		struct host_entry *he = calloc(1, sizeof(struct host_entry));
		he->ip = hdr->saddr;
		he->last_broadcast_id = ntohl(hdr->bid);
		skip_list_insert(op->known_hosts, &he->h, &hdr->saddr,
				 addr_cmp);
	} else {
		struct host_entry *he = skip_list_entry(hres,
		    struct host_entry, h);
		if (ntohl(hdr->bid) <= he->last_broadcast_id) {
			log_info("Packet broadcast id is less than the last"
			    "broadcast id recorded (%d < %d), possibly looping"
			    "packets. discarding...\n", ntohl(hdr->bid),
			    he->last_broadcast_id);
			return;
		} else
			he->last_broadcast_id = ntohl(hdr->bid);
	}

	if (hdr->daddr == op->myip) {
		//We are the target
		log_info("We are the target of RREQ, replying...\n");
		struct msg *nm = calloc(1, sizeof(struct msg));
		struct odr_hdr *xhdr = NULL;
		nm->buf = calloc(1, sizeof(struct odr_hdr));
		xhdr = nm->buf;
		xhdr->flags = htons(ODR_RREP);
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
		log_info("No route entry found, broadcasting...\n");
		int tmp = ntohs(hdr->hop_count);
		hdr->hop_count = htons(tmp+1);

		broadcast(op, op->buf, op->msg_len, addr->sll_ifindex);
	} else {
		log_info("Route entry found, sending RREP"
		    " on behalf of the target.\n");
		struct msg *nm = calloc(1, sizeof(struct msg));
		struct odr_hdr *xhdr = NULL;
		nm->buf = calloc(1, sizeof(struct odr_hdr));
		xhdr = nm->buf;
		xhdr->daddr = hdr->saddr;
		xhdr->saddr = hdr->daddr;
		xhdr->hop_count = re->hop_count;
		xhdr->flags = htons(ODR_RREP);
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
		int tmp = ntohs(hdr->hop_count);
		hdr->hop_count = htons(tmp+1);
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
		int tmp = ntohs(hdr->hop_count);
		hdr->hop_count = htons(tmp+1);
		struct msg *msg = calloc(1, sizeof(struct msg));
		msg->len = op->msg_len;
		msg->flags = 0;
		msg->buf = malloc(msg->len);
		memcpy(msg->buf, op->buf, msg->len);
		//Send, possibly queueing it
		send_msg(op, msg);
	} else
		//Deliver the packet
		op->cb((void *)(hdr+1), ntohs(hdr->payload_len), hdr->saddr,
		       op->cbdata);
}

static inline void
radv_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
	struct odr_hdr *hdr = op->buf;
	route_table_update(op, hdr->saddr, hdr->hop_count, addr);
}

static inline void
enlarge_buffer(struct odr_protocol *op, size_t s) {
	if (s <= op->buf_len)
		return;
	op->buf = realloc(op->buf, s);
	op->buf_len = s;
}

static void odr_read_cb(void *ml, void *data, int rw){
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

	int idx = addr.sll_ifindex;
	int mtu = op->ifi_table[idx].ifi_mtu;
	char *tmp = NULL;
	size_t iplen = 0;
	if (ret > mtu && mtu)
		log_warn("Packet larger than mtu (%d>%d)\n",
		    ret, mtu);
	enlarge_buffer(op, ret);
	ret = recvfrom(op->fd, op->buf, op->buf_len, 0,
	    (struct sockaddr *)&addr, &len);
	op->msg_len = ret;
	log_info("Packet coming in from %s(id:%d, ip:%s)\n",
	    op->ifi_table[idx].ifi_name, idx,
	    sa_ntop(op->ifi_table[idx].ifi_addr, &tmp, &iplen));
	free(tmp);

	struct odr_hdr *hdr = op->buf;
	dump_odr_hdr(hdr);
	int flags = ntohs(hdr->flags);
	if (flags & ODR_RREQ)
		rreq_handler(op, &addr);
	else if (flags & ODR_RREP)
		rrep_handler(op, &addr);
	else if (flags & ODR_DATA)
		data_handler(op, &addr);
	else if (flags & ODR_RADV)
		radv_handler(op, &addr);
}
void *odr_protocol_init(void *ml, data_cb cb, void *data,
			int stale, struct ifi_info *head) {
	int sockfd = socket(AF_PACKET, SOCK_DGRAM, htons(ODR_MAGIC));
	if (sockfd < 0) {
		log_err("Failed to create packet socket\n");
		return NULL;
	}
	struct odr_protocol *op = malloc(sizeof(struct odr_protocol));
	op->fd = sockfd;
	op->fh = fd_insert(ml, sockfd, FD_READ, odr_read_cb, op);
	op->buf = NULL;
	op->cb = cb;
	op->buf_len = 0;
	op->stale = stale;;
	op->cbdata = data;;
	op->route_table = calloc(1, sizeof(struct skip_list_head));
	op->known_hosts = calloc(1, sizeof(struct skip_list_head));
	op->pending_msgs = NULL;
	op->bid = 0;
	skip_list_init_head(op->route_table);
	skip_list_init_head(op->known_hosts);

	struct ifi_info *tmp = head;
	int max_idx = 0;
	while(tmp) {
		if (tmp->ifi_index > max_idx)
			max_idx = tmp->ifi_index;
		tmp = tmp->ifi_next;
	}
	op->max_idx = 0;
	op->ifi_table = calloc(max_idx+1, sizeof(struct ifi_info));
	for (tmp = head; tmp; tmp = tmp->ifi_next) {
		if (op->ifi_table[tmp->ifi_index].ifi_name[0])
			log_warn("Duplicated interface, shouldn't use"
			    " doalias\n");
		if (strcmp(tmp->ifi_name, "eth0") == 0) {
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
		if (!(tmp->ifi_flags & IFF_UP)) {
			log_info("Interface %s not up\n", tmp->ifi_name);
			continue;
		}
		log_info("Valid interface %s, %d\n", tmp->ifi_name, tmp->ifi_index);
		memcpy(op->ifi_table+tmp->ifi_index, tmp,
		       sizeof(struct ifi_info));
		if (tmp->ifi_index > op->max_idx)
			op->max_idx = tmp->ifi_index;
	}
	return op;
}
