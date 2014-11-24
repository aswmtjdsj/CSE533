#include <time.h>
#include <netdb.h>

#include "odr_hdr.h"
#include "mainloop.h"
#include "odr_protocol.h"
#include "skiplist.h"
#include "utils.h"
#include "log.h"
struct ifinfo {
	int halen;
	uint8_t hwaddr[8];
	char name[IFNAMSIZ];
	uint32_t ip;
	uint16_t flags;
	uint16_t mtu;
	int index;
};
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
	struct ifinfo *ifi_table;
	//messages held back because there's no route
	struct msg *pending_msgs;
};
struct host_entry {
	struct skip_list_head h;
	uint32_t ip;
	char *name;
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
get_route(struct odr_protocol *op, uint32_t daddr, struct route_entry **re) {
	struct skip_list_head *res = skip_list_find_le(op->route_table,
	    &daddr, addr_cmp);
	struct route_entry *tre;
	*re = NULL;
	tre = skip_list_entry(res, struct route_entry, h);
	if (!res || tre->dst_ip != daddr)
		return 1;

	uint64_t now = get_timestamp();

	if (tre->timestamp+op->stale < now)
		return 2;

	if (re)
		*re = tre;
	return 0;
}
static inline int
send_msg_dontqueue(struct odr_protocol *op, const struct msg *msg) {
	struct odr_hdr *hdr = msg->buf;
	if (hdr->daddr == op->myip) {
		//Send to local, deliever it directly
		op->cb((void *)(hdr+1), ntohs(hdr->payload_len), hdr->saddr,
		    op->cbdata);
		return 1;
	}
	struct route_entry *re;
	int rea = get_route(op, hdr->daddr, &re);
	if (rea == 1) {
		//No route to host
		log_info("Route to destination %s not found\n",
		    inet_ntoa((struct in_addr){hdr->daddr}));
		return 0;
	}
	if (rea == 2) {
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
	if (flags & ODR_FORCED)
		tmpfp += sprintf(tmpfp, "FORCED ");
	log_debug("\tFLAGS: %s\n", tmpf);
	log_debug("\tSource IP: %s\n",
	    inet_ntoa((struct in_addr){hdr->saddr}));
	log_debug("\tTarget IP: %s\n",
	    inet_ntoa((struct in_addr){hdr->daddr}));
	log_debug("\tHop count: %d\n", ntohs(hdr->hop_count));
	log_debug("\tBroadcast ID: %d\n", ntohl(hdr->bid));
}
static inline
int broadcast(struct odr_protocol *op, void *buf, size_t len, int exclude) {
	int i, c = 0;
	struct sockaddr_ll lladdr;
	memset(&lladdr, 0, sizeof(lladdr));
	lladdr.sll_protocol = htons(ODR_MAGIC);
	lladdr.sll_family = AF_PACKET;
	for(i=0; i<=op->max_idx; i++) {
		//Broadcast to all interfaces
		if (!(op->ifi_table[i].flags & IFF_UP))
			continue;
		if (i == exclude)
			continue;
		log_info("Send packet through %s (id: %d, ip: %s)\n",
		    op->ifi_table[i].name, i,
		    inet_ntoa((struct in_addr){op->ifi_table[i].ip}));
		memset(lladdr.sll_addr, 255, IFHWADDRLEN);
		lladdr.sll_ifindex = i;
		lladdr.sll_halen = op->ifi_table[i].halen;
		lladdr.sll_hatype = lladdr.sll_pkttype = 0;
		int ret = sendto(op->fd, buf, len, 0,
		    (struct sockaddr *)&lladdr, sizeof(lladdr));
		if (ret < 0)
			log_warn("Failed to send packet via %s: %s\n",
			    op->ifi_table[i].name, strerror(errno));
		else
			c++;
	}
	return c;
}
static inline
int send_msg(struct odr_protocol *op, struct msg *msg, int flags) {
	int ret;
	if (!flags)
		ret = send_msg_dontqueue(op, msg);
	else {
		//Force rediscovery
		log_info("Flag set, force rediscovery\n");
		ret = 0;
	}

	if (ret) {
		free(msg->buf);
		free(msg);
		return ret;
	}

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
	xhdr->flags = htons(ODR_RREQ|(flags?ODR_FORCED:0));
	xhdr->bid = htonl(op->bid++);
	xhdr->payload_len = 0;

	broadcast(op, buf, sizeof(struct odr_hdr), -1);
	free(buf);
	return 0;
}
int send_msg_api(struct odr_protocol *op, uint32_t dst_ip,
		 const void *buf, size_t len, int flags) {
	struct msg *msg = calloc(1, sizeof(struct msg));
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

	return send_msg(op, msg, flags);
}
//daddr and hop_count is in network order
static inline int
route_table_update(struct odr_protocol *op, struct odr_hdr *hdr,
		   struct sockaddr_ll *addr, int noradv) {
	uint32_t daddr = hdr->saddr;
	uint32_t hop_count = ntohs(hdr->hop_count);
	uint16_t flags = ntohs(hdr->flags);
	int ret = 1;
	//Update route table from information in the packet
	if (daddr == op->myip)
		//No action needed
		return 0;

	log_info("Updating route table due to:\n");
	if (flags & ODR_RADV) {
		if (flags & ODR_DATA)
			log_info("\tData packet (already forwarded by another "
			    "node, we are just using the infomation to update"
			    " route table)\n");
		else if (flags & ODR_RREQ)
			log_info("\tRREQ (RREP already sent by another node)\n");
		else if (flags & ODR_RREP)
			log_info("\tRREP (already forwarded by another node)\n");
		else
			log_info("\tRoute advertisement\n");
	} else if (flags & ODR_DATA)
		log_info("\tData packet\n");
	else if (flags & ODR_RREQ)
		log_info("\tRREQ\n");
	else if (flags & ODR_RREP)
		log_info("\tRREP\n");

	struct skip_list_head *res = skip_list_find_le(op->route_table,
	    &daddr, addr_cmp);
	struct route_entry *re = skip_list_entry(res, struct route_entry, h);
	struct skip_list_head *hres = skip_list_find_le(op->known_hosts,
	    &daddr, addr_cmp);
	struct host_entry *he = skip_list_entry(hres, struct host_entry, h);
	const char *sname = "Unknown host";
	if (hres && he->ip == daddr)
		sname = he->name;

	if (!res || re->dst_ip != daddr) {
		log_info("New route to %s(%s) through %s, hop: %d\n",
		    inet_ntoa((struct in_addr){daddr}), sname,
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
		if (re->hop_count > hop_count || flags & ODR_FORCED) {
			log_info("Update route to %s(%s) through %s, hop: %d"
			    " to through %s, hop: %d %s\n",
			    inet_ntoa((struct in_addr){daddr}), sname,
			    mac_tostring(re->route_mac, re->halen),
			    re->hop_count,
			    mac_tostring(addr->sll_addr, addr->sll_halen),
			    hop_count,
			    flags&ODR_FORCED ? "(forced)" : "");
			re->hop_count = hop_count;
			re->ifi_idx = addr->sll_ifindex;
			memcpy(re->route_mac, addr->sll_addr, addr->sll_halen);
			re->halen = addr->sll_halen;
			re->timestamp = get_timestamp();
		} else if (re->hop_count == hop_count) {
			log_info("Update timestamp on route to %s(%s) through "
			    "%s, hop:%d\n", inet_ntoa((struct in_addr){daddr}),
			    sname, mac_tostring(re->route_mac, re->halen),
			    re->hop_count);
			re->timestamp = get_timestamp();
			noradv = 1;
			ret = 0;
		} else {
			log_info("Nothing to update, route table unchanged\n");
			return 0;
		}
	}

	if (!noradv) {
		void *buf = malloc(sizeof(struct odr_hdr));
		struct odr_hdr *xhdr = buf;
		xhdr->flags = htons(ODR_RADV|flags);
		xhdr->hop_count = htons(hop_count+1);
		xhdr->payload_len = 0;
		xhdr->bid = hdr->bid;
		xhdr->daddr = 0;
		xhdr->saddr = daddr;

		log_info("Route updated due to RREQ, now we are advertising "
		    "propagate the RREQ to our neighbours, except where it "
		    "came in (%d)\n", addr->sll_ifindex);

		broadcast(op, buf, sizeof(struct odr_hdr), addr->sll_ifindex);
		free(buf);
	}

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
		int ret = send_msg_dontqueue(op, tmp);
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
	return ret;
}
static inline struct host_entry *
host_entry_update(struct odr_protocol *op, uint32_t ip, uint32_t bid) {
	//Looking up the source
	struct skip_list_head *hres = skip_list_find_le(op->known_hosts,
	    &ip, addr_cmp);
	struct host_entry *he = skip_list_entry(hres, struct host_entry, h);
	if (!hres || he->ip != ip) {
		struct host_entry *he = calloc(1, sizeof(struct host_entry));
		he->ip = ip;
		he->last_broadcast_id = bid;

		struct in_addr tmpaddr;
		tmpaddr.s_addr = ip;
		struct hostent *h = gethostbyaddr(&tmpaddr, sizeof(tmpaddr),
		    AF_INET);
		he->name = strdup(h->h_name);
		skip_list_insert(op->known_hosts, &he->h, &he->ip,
				 addr_cmp);
		return NULL;
	} else {
		if (bid <= he->last_broadcast_id)
			return he;
		else {
			he->last_broadcast_id = bid;
			return NULL;
		}
	}
}
static inline void
rreq_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
	struct odr_hdr *hdr = op->buf;
	uint16_t flags = ntohs(hdr->flags);
	if (ntohl(hdr->bid) > op->bid)
		op->bid = ntohl(hdr->bid)+1;

	struct host_entry *he = host_entry_update(op, hdr->saddr,
	    ntohl(hdr->bid));
	if (he) {
		log_info("Packet broadcast id is less than the last"
		    "broadcast id recorded (%d < %d), duplicated RREQ.\n",
		    ntohl(hdr->bid),
		    he->last_broadcast_id+1);
		//Removing the forced bit
		flags &= ~ODR_FORCED;
		hdr->flags = htons(flags);
	}

	if (hdr->daddr == op->myip) {
		//We are the target
		int ret = route_table_update(op, hdr, addr, 0);
		if (he && ret == 0) {
			log_info("We already replied to this duplicated RREQ, "
			    "and this is not a better route, won't reply.\n");
			return;
		}
		log_info("We are the target of RREQ, replying...\n");
		struct msg *nm = calloc(1, sizeof(struct msg));
		struct odr_hdr *xhdr = NULL;
		nm->buf = calloc(1, sizeof(struct odr_hdr));
		xhdr = nm->buf;
		xhdr->flags = htons(ODR_RREP|(flags&ODR_FORCED));
		xhdr->saddr = op->myip;
		xhdr->daddr = hdr->saddr;
		xhdr->hop_count = 0;
		xhdr->bid = htonl(op->bid++);
		nm->len = sizeof(struct odr_hdr);

		ret = send_msg_dontqueue(op, nm);
		if (!ret)
			log_warn("Can't find route for RREP, IMPOSSIBLE\n");
		free(nm->buf);
		free(nm);
		return;
	}
	struct route_entry *re;
	int rea = get_route(op, hdr->daddr, &re);
	if (rea || (flags & ODR_FORCED)) {
		//Not found
		int ret = route_table_update(op, hdr, addr, 1);
		//if ret = 1: better route, we want to reply but
		//	      there's no route, so we broadcast, so noadv=1
		//if ret = 0: no better route, we don't want to reply, and
		//	      there's no need to propagate, so noadv=1
		if (ret == 0 && he) {
			log_info("We already replied to this duplicated RREQ, "
			    "and this is not a better route, won't reply.\n");
			return;
		}
		if (flags & ODR_FORCED)
			log_info("Forced rediscovery, broadcasting...\n");
		else if (rea == 1)
			log_info("No route entry found, broadcasting...\n");
		else
			log_info("Route entry staled, broadcasting...\n");
		int tmp = ntohs(hdr->hop_count);
		hdr->hop_count = htons(tmp+1);

		broadcast(op, op->buf, op->msg_len, addr->sll_ifindex);
	} else {
		int ret = route_table_update(op, hdr, addr, 0);
		if (he && ret == 0) {
			log_info("We already replied to this duplicated RREQ, "
			    "and this is not a better route, won't reply.\n");
			return;
		}
		log_info("Route entry found, sending RREP"
		    " on behalf of the target.\n");
		struct msg *nm = calloc(1, sizeof(struct msg));
		struct odr_hdr *xhdr = NULL;
		nm->buf = calloc(1, sizeof(struct odr_hdr));
		xhdr = nm->buf;
		xhdr->daddr = hdr->saddr;
		xhdr->saddr = hdr->daddr;
		xhdr->hop_count = htons(re->hop_count+1);
		xhdr->flags = htons(ODR_RREP);
		xhdr->bid = htonl(op->bid++);
		nm->len = sizeof(struct odr_hdr);

		ret = send_msg_dontqueue(op, nm);
		if (!ret)
			log_warn("Can't find route for RREP, IMPOSSIBLE\n");
		return;
	}
}
static inline void
rrep_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
	struct odr_hdr *hdr = op->buf;
	struct host_entry *he = host_entry_update(op, hdr->saddr,
	    ntohl(hdr->bid));
	if (he) {
		log_debug("duplicated RREP, removing forced bit.\n");
		hdr->flags = htons(ntohs(hdr->flags) & ~ODR_FORCED);
	}
	route_table_update(op, hdr, addr, 1);

	if (hdr->daddr != op->myip) {
		//Route the RREP
		int tmp = ntohs(hdr->hop_count);
		hdr->hop_count = htons(tmp+1);
		struct msg *msg = calloc(1, sizeof(struct msg));
		msg->len = op->msg_len;
		msg->buf = malloc(msg->len);
		memcpy(msg->buf, op->buf, msg->len);
		//Send, possibly queueing it
		send_msg(op, msg, 0);
	}
}

static inline void
data_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
	struct odr_hdr *hdr = op->buf;
	host_entry_update(op, hdr->saddr, 0);
	route_table_update(op, hdr, addr, 1);

	if (hdr->daddr != op->myip) {
		//Route the packet
		int tmp = ntohs(hdr->hop_count);
		hdr->hop_count = htons(tmp+1);
		struct msg *msg = calloc(1, sizeof(struct msg));
		msg->len = op->msg_len;
		msg->buf = malloc(msg->len);
		memcpy(msg->buf, op->buf, msg->len);
		//Send, possibly queueing it
		send_msg(op, msg, 0);
	} else
		//Deliver the packet
		op->cb((void *)(hdr+1), ntohs(hdr->payload_len), hdr->saddr,
		       op->cbdata);
}

static inline void
radv_handler(struct odr_protocol *op, struct sockaddr_ll *addr) {
	struct odr_hdr *hdr = op->buf;
	uint16_t flags = ntohs(hdr->flags);
	if (!(flags&ODR_RREQ)) {
		log_debug("malformed radv (no rreq flag)\n");
		return;
	}

	struct host_entry *he =
	    host_entry_update(op, hdr->saddr, ntohl(hdr->bid));
	if (he) {
		log_debug("duplicated radv (bid already seen in a rreq)\n");
		hdr->flags = htons(flags&~ODR_FORCED);
	}
	route_table_update(op, hdr, addr, 0);
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
	int mtu = op->ifi_table[idx].mtu;
	if (ret > mtu && mtu)
		log_warn("Packet larger than mtu (%d>%d)\n",
		    ret, mtu);
	enlarge_buffer(op, ret);
	ret = recvfrom(op->fd, op->buf, op->buf_len, 0,
	    (struct sockaddr *)&addr, &len);
	op->msg_len = ret;
	log_info("Packet coming in from %s(id:%d, ip:%s)\n",
	    op->ifi_table[idx].name, idx,
	    inet_ntoa((struct in_addr){op->ifi_table[idx].ip}));

	struct odr_hdr *hdr = op->buf;
	dump_odr_hdr(hdr);
	int flags = ntohs(hdr->flags);
	if (flags & ODR_RADV)
		radv_handler(op, &addr);
	else if (flags & ODR_RREQ)
		rreq_handler(op, &addr);
	else if (flags & ODR_RREP)
		rrep_handler(op, &addr);
	else if (flags & ODR_DATA)
		data_handler(op, &addr);
}
void *odr_protocol_init(void *ml, data_cb cb, void *data, int stale) {
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
	op->bid = 1;
	op->myip = 0;
	skip_list_init_head(op->route_table);
	skip_list_init_head(op->known_hosts);

	struct ifi_info *head = get_ifi_info(AF_INET, 0), *tmp;
	int max_idx = 0;
	tmp = head;
	while(tmp) {
		if (tmp->ifi_index > max_idx)
			max_idx = tmp->ifi_index;
		tmp = tmp->ifi_next;
	}
	op->max_idx = 0;
	op->ifi_table = calloc(max_idx+1, sizeof(struct ifi_info));
	for (tmp = head; tmp; tmp = tmp->ifi_next) {
		struct ifinfo *ife = &op->ifi_table[tmp->ifi_index];
		struct sockaddr_in *s = (struct sockaddr_in *)
		    tmp->ifi_addr;
		if (ife->name[0])
			log_warn("Duplicated interface, shouldn't use"
			    " doalias\n");
		if (strcmp(tmp->ifi_name, "eth0") == 0) {
			log_info("Ignoring eth0 (but recording the ip)\n");
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
		ife->ip = s->sin_addr.s_addr;
		ife->flags = tmp->ifi_flags;
		ife->halen = IFHWADDRLEN;
		ife->mtu = tmp->ifi_mtu;
		ife->index = tmp->ifi_index;
		memcpy(ife->hwaddr, tmp->ifi_hwaddr, sizeof(ife->hwaddr));
		if (tmp->ifi_index > op->max_idx)
			op->max_idx = tmp->ifi_index;
	}
	free_ifi_info(head);
	return op;
}
