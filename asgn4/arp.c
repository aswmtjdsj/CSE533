#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/un.h>
#include <net/ethernet.h>
#include <stdbool.h>
#include <linux/if_arp.h>
#include "const.h"
#include "skiplist.h"
#include "arp_protocol.h"
#include "arp_client.h"
#include "mainloop.h"

struct ifinfo {
	int halen;
	uint8_t hwaddr[8];
	char name[IFNAMSIZ];
	uint32_t ip;
	uint16_t flags;
	uint16_t mtu;
	int index;
};

struct ip_record {
	uint32_t ip;
	struct skip_list_head h;
};

struct client {
	void *fh;
	struct arp_protocol *p;
	struct cache_entry *owner;
	struct skip_list_head h;
};

struct cache_entry {
	uint32_t ip;
	uint8_t hwaddr[8];
	uint32_t ifindex;
	bool incomplete;
	uint32_t pending_id;
	uint32_t client_count;
	struct skip_list_head clients;
	struct skip_list_head h;
};

struct arp_protocol {
	int fd, areq_fd;
	uint16_t hatype;
	uint8_t halen;
	uint8_t hwaddr[8];
	int eth0_ifidx;
	void *ml;
	struct skip_list_head myip;
	struct skip_list_head cache;
};

static inline int addr_cmp(struct skip_list_head *h, const void *b) {
	struct ip_record *a = skip_list_entry(h, struct ip_record, h);
	const uint32_t *bb = b;
	return a->ip-(*bb);
}

static inline int cache_cmp(struct skip_list_head *h, const void *b) {
	struct cache_entry *a = skip_list_entry(h, struct cache_entry, h);
	const uint32_t *bb = b;
	return a->ip-(*bb);
}

static inline int client_cmp(struct skip_list_head *h, const void *b) {
	struct client *a = skip_list_entry(h, struct client, h);
	const int *bb = b;
	return fd_get_fd(a->fh)-(*bb);
}

static inline void
arp_dump(const struct arp *msg) {
	log_info("ARP packet dump:\n");
	log_info("\thlen: %u\n", msg->hlen);
	log_info("\tplen: %u\n", msg->plen);
	log_info("\tptype: %04X\n", ntohs(msg->ptype));
	log_info("\thatype: %04X\n", ntohs(msg->hatype));
	log_info("\toper: %u\n", ntohs(msg->oper));

	uint32_t saddr, taddr;
	memcpy(&saddr, msg->data+msg->hlen, 4);
	memcpy(&taddr, msg->data+msg->hlen*2+msg->plen, 4);

	log_info("\tsource ip: %s\n", inet_ntoa((struct in_addr){saddr}));
	log_info("\ttarget ip: %s\n", inet_ntoa((struct in_addr){taddr}));
	int i;
	char buf[30];
	for (i = 0; i < msg->hlen; i++)
		sprintf(buf+3*i, "%02X:", msg->data[i]);
	buf[3*msg->hlen-1] = 0;
	log_info("\tsource hardware address: %s\n", buf);
	for (i = 0; i < msg->hlen; i++)
		sprintf(buf+3*i, "%02X:", msg->data[msg->hlen+msg->plen+i]);
	buf[3*msg->hlen-1] = 0;
	log_info("\ttarget hardware address: %s\n", buf);
}

static inline void
arp_send_request(struct sockaddr *addr, struct arp_protocol *p) {
	uint8_t buf[1500];
	struct ether_header *ehdr = (void *)buf;
	struct arp *msg = (void *)(ehdr+1);
	struct skip_list_head *tmp = p->myip.next[0];
	struct ip_record *ptr = skip_list_entry(tmp, struct ip_record, h);
	msg->hatype = htons(p->hatype);
	msg->hlen = p->halen;
	ehdr->ether_type = htons(ARP_MAGIC);
	memcpy(ehdr->ether_shost, p->hwaddr, ETH_ALEN);
	memcpy(msg->data, p->hwaddr, p->halen);
	msg->oper = htons(ARPOP_REQUEST);

	struct sockaddr_in *sin = (void *)addr;
	switch (addr->sa_family) {
		case AF_INET:
			msg->ptype = htons(ETHERTYPE_IP);
			msg->plen = 4;
			memcpy(msg->data+msg->hlen, &ptr->ip, 4);
			log_debug("XXX%s\n", inet_ntoa((struct in_addr){ptr->ip}));
			memset(msg->data+msg->hlen+msg->plen, 255, p->halen);
			memcpy(msg->data+msg->hlen*2+msg->plen, &sin->sin_addr, 4);
			break;
		default:
			log_err("Unsupported address family %u\n",
				addr->sa_family);
			return;
	}

	struct sockaddr_ll lladdr;
	lladdr.sll_ifindex = p->eth0_ifidx;
	lladdr.sll_protocol = 0;
	lladdr.sll_halen = ETH_ALEN;
	lladdr.sll_family = AF_PACKET;
	lladdr.sll_hatype = lladdr.sll_pkttype = 0;
	memset(lladdr.sll_addr, 0xff, ETH_ALEN);
	memset(ehdr->ether_dhost, 0xff, ETH_ALEN);

	log_info("Sending following packet\n");
	arp_dump(msg);
	int ret = sendto(p->fd, buf,
	    sizeof(struct arp)+sizeof(struct ether_header),
	    0, (void *)&lladdr, sizeof(lladdr));
	if (ret < 0)
		log_warn("sendto via interface %d failed, %s\n",
		    p->eth0_ifidx, strerror(errno));
	return;
}

void areq_callback(void *ml, void *data, int rw) {
	struct client *nc = data;
	uint32_t ip;
	struct sockaddr caddr;
	struct arp_protocol *p = nc->p;
	socklen_t len;
	int fd = fd_get_fd(nc->fh);
	int ret = recvfrom(fd, &ip, 4, 0, &caddr, &len);
	if (ret <= 0) {
		if (ret < 0)
			log_warn("Failed to recv\n");
		else
			log_info("Remote socket closed\n");
		if (nc->owner) {
			skip_list_delete(&nc->h);
			nc->owner->client_count--;
		}
		close(fd);
		free(nc);
		return;
	}

	struct skip_list_head *res =
	    skip_list_find_eq(&p->cache, &ip, cache_cmp);
	struct cache_entry *ce = skip_list_entry(res, struct cache_entry, h);
	if (!res) {
		ce = talloc(1, struct cache_entry);
		skip_list_init_head(&ce->clients);
		ce->client_count = 1;
		ce->incomplete = true;
		ce->ip = ip;
		skip_list_insert(&p->cache, &ce->h, &ip, cache_cmp);
	} else {
		struct cache_entry *ce = skip_list_entry(res,
		    struct cache_entry, h);
		if (ce->incomplete) {
			int fd = fd_get_fd(nc->fh);
			skip_list_insert(&ce->clients, &nc->h,
			    &fd, client_cmp);
			ce->client_count++;
			return;
		} else {
			log_info("Address exists in cache, replying...\n");
			struct hwaddr reply;
			reply.sll_ifindex = htonl(ce->ifindex);
			reply.sll_halen = ETH_ALEN;
			memcpy(reply.sll_addr, ce->hwaddr, ETH_ALEN);
			reply.sll_hatype = htons(ARPHRD_ETHER);

			int ret = send(fd, &reply, sizeof(reply), 0);
			if (ret < 0)
				log_err("Send areq reply failed %s\n",
				    strerror(errno));
			close(fd);
		}
	}

	skip_list_insert(&ce->clients, &nc->h, &fd, client_cmp);
	nc->owner = ce;

	struct sockaddr_in req_addr;
	req_addr.sin_addr.s_addr = ip;
	req_addr.sin_family = AF_INET;
	arp_send_request((void *)&req_addr, p);
}

void send_areq_reply(struct cache_entry *ce, struct arp_protocol *p) {
	assert(!ce->incomplete);
	struct hwaddr reply;
	reply.sll_ifindex = htonl(ce->ifindex);
	reply.sll_halen = ETH_ALEN;
	memcpy(reply.sll_addr, ce->hwaddr, ETH_ALEN);
	reply.sll_hatype = htons(ARPHRD_ETHER);
	struct skip_list_head *tmp, *tmp2;
	struct client *c;
	ce->client_count = 0;
	skip_list_foreach_safe(&ce->clients, tmp, tmp2, c,
	    struct client, h) {
		int fd = fd_get_fd(c->fh);
		int ret = send(fd, &reply, sizeof(reply), 0);
		if (ret < 0)
			log_err("Send areq reply failed %s\n",
			    strerror(errno));
		close(fd);
		skip_list_delete(&c->h);
		fd_remove(c->fh);
		free(c);
	}
}

void arp_req_callback(void *ml, const void *buf, struct sockaddr_ll *addr,
		      struct arp_protocol *p) {
	const struct ether_header *ehdr;
	const struct arp *msg;
	uint8_t rbuf[1500];
	struct ether_header *rehdr = (void *)rbuf;
	struct arp *rmsg = (void *)(rehdr+1);

	log_info("Received arp request:\n");

	ehdr = (const void *)buf;
	msg = (const void *)(ehdr+1);
	arp_dump(msg);

	uint16_t ptype = ntohs(msg->ptype);
	uint32_t addrv4, saddrv4;
	if (ntohs(msg->hatype) != ARPHRD_ETHER) {
		log_warn("Unsupported link layer protocol\n");
		return;
	}
	if (msg->hlen != ETH_ALEN) {
		log_warn("Wrong hardware address length\n");
		return;
	}
	if (msg->plen != 4) {
		log_warn("Wrong protocol address length\n");
		return;
	}
	switch(ptype) {
		case ETHERTYPE_IP:
			memcpy(&addrv4, msg->data+msg->plen+msg->hlen*2, 4);
			memcpy(&saddrv4, msg->data+msg->hlen, 4);
			break;
		default:
			log_warn("Unsupported ether type %04X\n", ptype);
			break;
	}

	struct skip_list_head *res = skip_list_find_eq(&p->myip,
	    &addrv4, addr_cmp);
	struct skip_list_head *cres = skip_list_find_eq(&p->cache,
	    &saddrv4, cache_cmp);
	log_info("Target ip is %s\n", inet_ntoa((struct in_addr){addrv4}));

	if (!res) {
		//Not for me
		log_info("Target of the arp request is not me\n");
		if (cres) {
			struct cache_entry *ce = skip_list_entry(cres,
			    struct cache_entry, h);
			if (ce->incomplete)
				return;
			log_info("Updating cache entry\n");
			//Update cache entry
			if (memcmp(ce->hwaddr, msg->data, ETH_ALEN) == 0)
				return;
			memcpy(ce->hwaddr, msg->data, ETH_ALEN);
		}
	} else {
		//Reply and update cache
		//Copy source addr to dst addr
		log_info("Target is me, construct reply\n");
		memcpy(rmsg, msg, sizeof(*msg)-sizeof(msg->data));
		memcpy(rmsg->data+rmsg->plen+rmsg->hlen, msg->data,
		    rmsg->plen+rmsg->hlen);
		assert(p->halen == msg->hlen);
		assert(msg->hlen == ETH_ALEN);
		rehdr->ether_type = htons(ARP_MAGIC);
		memcpy(rehdr->ether_dhost, msg->data, ETH_ALEN);
		memcpy(rmsg->data, p->hwaddr, ETH_ALEN);
		memcpy(rehdr->ether_shost, p->hwaddr, ETH_ALEN);
		memcpy(rmsg->data+rmsg->hlen, &addrv4, 4);
		rmsg->oper = htons(ARPOP_REPLY);
		log_info("Sending arp reply\n");
		arp_dump(rmsg);

		struct sockaddr_ll lladdr;
		lladdr.sll_ifindex = addr->sll_ifindex;
		memcpy(lladdr.sll_addr, ehdr->ether_dhost, ETH_ALEN);
		lladdr.sll_protocol = htons(ARP_MAGIC);
		lladdr.sll_family = AF_PACKET;
		int ret = sendto(p->fd, rbuf, sizeof(struct ether_header)+
		    sizeof(struct arp), 0, (void *)&lladdr, sizeof(lladdr));
		if (ret < 0)
			log_warn("Send reply failed %s\n", strerror(errno));
		log_info("Update cache entry from source addresses\n");
		if (cres) {
			struct cache_entry *ce = skip_list_entry(cres,
			    struct cache_entry, h);
			ce->ifindex = addr->sll_ifindex;
			memcpy(ce->hwaddr, msg->data, ETH_ALEN);
			if (ce->incomplete) {
				send_areq_reply(ce, p);
				ce->incomplete = false;
			}
		} else {
			struct cache_entry *ce = talloc(1, struct cache_entry);
			ce->ifindex = addr->sll_ifindex;
			ce->incomplete = false;
			skip_list_init_head(&ce->clients);
			ce->client_count = 0;
			ce->ip = addrv4;
			memcpy(ce->hwaddr, msg->data, ETH_ALEN);
			skip_list_insert(&p->cache, &ce->h, &ce->ip, cache_cmp);
		}
	}
}

void arp_reply_callback(void *ml, void *buf, struct sockaddr_ll *addr,
			struct arp_protocol *p) {
	struct ether_header *ehdr = buf;
	struct arp *msg = (void *)(ehdr+1);

	log_info("Received arp reply\n");
	arp_dump(msg);

	if (ntohs(msg->hatype) != p->hatype) {
		log_err("Received reply from a different type of network\n");
		return;
	}
	if (ntohs(msg->ptype) != ETHERTYPE_IP) {
		log_err("Received reply for unsupported protocol\n");
		return;
	}
	if (msg->plen != 4) {
		log_err("Wrong protocol address length\n");
		return;
	}
	if (msg->hlen != ETH_ALEN) {
		log_err("Wrong hardware address length\n");
		return;
	}
	uint32_t addrv4, daddrv4;
	memcpy(&addrv4, msg->data+msg->hlen, 4);
	memcpy(&daddrv4, msg->data+2*msg->hlen+msg->plen, 4);

	struct skip_list_head *res = skip_list_find_eq(&p->myip,
	    &daddrv4, addr_cmp);
	if (!res) {
		log_err("Destination IP address doesn't exist at local\n");
		return;
	}
	if (memcmp(msg->data+msg->hlen+msg->plen, p->hwaddr, ETH_ALEN) != 0) {
		log_err("Destination hardware address doesn't match\n");
		return;
	}

	res = skip_list_find_eq(&p->cache, &addrv4, cache_cmp);
	if (!res) {
		log_err("Cache entry doesn't exist, either invalid packet, or"
		    "the client requested it has died. Discarding...\n");
		return;
	}

	struct cache_entry *ce = skip_list_entry(res, struct cache_entry, h);
	if (!ce->incomplete) {
		log_err("Duplicated replies received, dicarding...\n");
		return;
	}
	ce->incomplete = false;
	ce->ifindex = addr->sll_ifindex;
	memcpy(ce->hwaddr, msg->data, ETH_ALEN);
	send_areq_reply(ce, p);
}
void listen_cb(void *ml, void *data, int rw) {
	struct arp_protocol *p = data;
	struct sockaddr caddr;
	socklen_t len = sizeof(caddr);
	int fd = accept(p->areq_fd, &caddr, &len);

	struct client *nc = talloc(1, struct client);
	nc->fh = fd_insert(ml, fd, FD_READ, areq_callback, nc);
	nc->owner = NULL;
	nc->p = p;
}
void arp_callback(void *ml, void *data, int rw) {
	struct arp_protocol *p = data;
	uint8_t buf[1500];
	struct sockaddr_ll lladdr;
	struct ether_header *ehdr = (void *)buf;
	struct arp *msg = (void *)(ehdr+1);
	socklen_t len;

	int ret = recvfrom(p->fd, buf, sizeof(buf), 0,
	    (void *)&lladdr, &len);
	if (ret < 0) {
		log_err("Failed to recv from raw socket\n");
		return;
	}

	uint16_t o = ntohs(msg->oper);
	if (o == ARPOP_REQUEST)
		arp_req_callback(ml, buf, &lladdr, p);
	else if (o == ARPOP_REPLY)
		arp_reply_callback(ml, buf, &lladdr, p);
	else
		log_warn("Unknown arp packet\n");
}
static struct arp_protocol p;
int main(int argc, const char **argv) {
	skip_list_init_head(&p.myip);
	skip_list_init_head(&p.cache);
	struct ifi_info *head = get_ifi_info(AF_INET, 1), *tmp;
	char *iptmp = NULL;
	size_t len = 0;
	tmp = head;
	for (tmp = head; tmp; tmp = tmp->ifi_next) {
		struct sockaddr_in *s = (struct sockaddr_in *)
		    tmp->ifi_addr;
		if (strcmp(tmp->ifi_name, "eth0") == 0) {
			log_debug("Getting local ip addresses from eth0\n");
			struct ip_record *ir = talloc(1, struct ip_record);
			log_info("Local ip: %s\n", sa_ntop((void *)s,
			    &iptmp, &len));
			ir->ip = s->sin_addr.s_addr;
			p.eth0_ifidx = tmp->ifi_index;
			skip_list_insert(&p.myip, &ir->h, &ir->ip, addr_cmp);
			if (tmp->ifi_halen != ETH_ALEN)
				log_info("Unsupported network device %u\n", tmp->ifi_halen);
			else {
				memcpy(p.hwaddr, tmp->ifi_hwaddr, tmp->ifi_halen);
				p.halen = tmp->ifi_halen;
				p.hatype = tmp->ifi_hatype;
			}
		}
	}
	free_ifi_info(head);

	struct sockaddr_un addr_un;
	addr_un.sun_family = AF_UNIX;
	memset(addr_un.sun_path, 0, sizeof(addr_un.sun_path));
	strcpy(addr_un.sun_path+1, "arp_" ID);

	int areq_sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (areq_sock < 0) {
		log_err("Failed to create socket %s\n",
		    strerror(errno));
		return 1;
	}

	int ret = bind(areq_sock, (void *)&addr_un, sizeof(addr_un));
	if (ret < 0) {
		log_err("Failed to bind %s\n",
		    strerror(errno));
		return 1;
	}

	ret = listen(areq_sock, 0);
	if (ret < 0) {
		log_err("Failed to listen %s\n",
		    strerror(errno));
		return 1;
	}
	p.areq_fd = areq_sock;

	p.ml = mainloop_new();
	fd_insert(p.ml, areq_sock, FD_READ, listen_cb, &p);

	p.fd = socket(AF_PACKET, SOCK_RAW, htons(ARP_MAGIC));
	fd_insert(p.ml, p.fd, FD_READ, arp_callback, &p);

	mainloop_run(p.ml);
	return 0;
}
