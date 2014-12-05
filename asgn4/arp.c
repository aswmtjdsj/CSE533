#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/ethernet.h>
#include <stdbool.h>
#include "const.h"
#include "skiplist.h"
#include "arp_protocol.h"

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
	int fd;
	struct client *next;
};

struct cache_entry {
	uint32_t ip;
	uint8_t hwaddr[8];
	uint32_t ifindex;
	bool incomplete;
	uint32_t client_count;
	struct client *c;
	struct skip_list_head h;
};

struct arp_protocol {
	int fd;
	uint16_t hatype;
	uint8_t halen;
	uint8_t hwaddr[8];
	int max_ifidx;
	struct ifinfo *ifi_table;
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

int arp_request(struct sockaddr *addr, struct arp_protocol *p) {
	uint8_t buf[1060];
	struct ether_header *ehdr = (void *)buf;
	struct arp *msg = (void *)(ehdr+1);
	struct skip_list_head *tmp = p->myip.next[0];
	struct ip_record *ptr = skip_list_entry(tmp, struct ip_record, h);
	msg->hatype = p->hatype;
	msg->hlen = p->halen;
	ehdr->ether_type = htons(ARP_MAGIC);
	memcpy(ehdr->ether_shost, p->hwaddr, ETH_ALEN);
	memcpy(msg->data, p->hwaddr, p->halen);
	memcpy(msg->data, p->hwaddr, p->halen);

	int pos = p->halen;
	struct sockaddr_in *sin = (void *)addr;
	switch (addr->sa_family) {
		case AF_INET:
			msg->ptype = htons(ETHERTYPE_IP);
			msg->plen = 4;
			memcpy(msg->data+pos, &ptr->ip, 4);
			pos += 4;
			memset(msg->data+pos, 255, p->halen);
			pos += p->halen;
			memcpy(msg->data+pos, &sin->sin_addr, 4);
			break;
		default:
			log_err("Unsupported address family %u\n",
				addr->sa_family);
			return 1;
	}

	struct sockaddr_ll lladdr;
	int i;
	for (i = 0; i < p->max_ifidx; i++) {
		if (!p->ifi_table[i].flags & IFF_UP)
			continue;
		lladdr.sll_ifindex = i;
		lladdr.sll_protocol = 0;
		lladdr.sll_halen = ETH_ALEN;
		lladdr.sll_family = AF_PACKET;
		lladdr.sll_hatype = lladdr.sll_pkttype = 0;
		memset(lladdr.sll_addr, 0xff, ETH_ALEN);
		memset(ehdr->ether_dhost, 0xff, ETH_ALEN);

		int ret = sendto(p->fd, buf,
		    sizeof(struct arp)+sizeof(struct ether_header),
		    0, (void *)&lladdr, sizeof(lladdr));
		if (ret)
			log_warn("sendto via interface %d failed, %s\n",
			    i, strerror(errno));
	}
	return 0;
}

void send_areq_reply(struct cache_entry *ce, struct arp_protocol *p) {
}

void arp_req_callback(void *ml, void *buf, struct sockaddr_ll *addr,
		      struct arp_protocol *p) {
	struct ether_header *ehdr;
	struct arp *msg;

	ehdr = (void *)buf;
	msg = (void *)(ehdr+1);

	uint16_t ptype = ntohs(msg->ptype);
	uint32_t addrv4, saddrv4;
	switch(ptype) {
		case ETHERTYPE_IP:
			memcpy(msg->data+msg->plen+msg->hlen, &addrv4, 4);
			memcpy(msg->data+msg->hlen, &saddrv4, 4);
			break;
		default:
			log_warn("Unsupported ether type %04X\n", ptype);
			break;
	}
	struct skip_list_head *res = skip_list_find_eq(&p->myip,
	    &addrv4, addr_cmp);
	struct skip_list_head *cres = skip_list_find_eq(&p->cache,
	    &saddrv4, cache_cmp);

	if (!res) {
		//Not for me
		if (cres) {
			struct cache_entry *ce = skip_list_entry(cres,
			    struct cache_entry, h);
			if (ce->incomplete)
				return;
			//Update cache entry
			if (memcmp(ce->hwaddr, msg->data, ETH_ALEN) == 0)
				return;
			memcpy(ce->hwaddr, msg->data, ETH_ALEN);
		}
	} else {
		//Reply and update cache
		//Copy source addr to dst addr
		memcpy(msg->data+msg->plen+msg->hlen, msg->data,
		    msg->plen+msg->hlen);
		assert(p->halen == msg->hlen);
		assert(msg->hlen == ETH_ALEN);
		ehdr->ether_type = htons(ARP_MAGIC);
		memcpy(ehdr->ether_dhost, msg->data, ETH_ALEN);
		memcpy(msg->data, p->hwaddr, ETH_ALEN);
		memcpy(ehdr->ether_shost, p->hwaddr, ETH_ALEN);
		memcpy(msg->data+msg->hlen, &addrv4, 4);

		struct sockaddr_ll lladdr;
		lladdr.sll_ifindex = addr->sll_ifindex;
		memcpy(lladdr.sll_addr, ehdr->ether_dhost, ETH_ALEN);
		lladdr.sll_protocol = htons(ARP_MAGIC);
		lladdr.sll_family = AF_PACKET;
		int ret = sendto(p->fd, buf, sizeof(struct ether_header)+
		    sizeof(struct arp), 0, (void *)&lladdr, sizeof(lladdr));
		if (ret)
			log_warn("Send reply failed %s\n", strerror(errno));
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
			ce->c = NULL;
			ce->client_count = 0;
			ce->ip = addrv4;
			memcpy(ce->hwaddr, msg->data, ETH_ALEN);
			skip_list_insert(&p->cache, &ce->h, &ce->ip, cache_cmp);
		}
	}
}

void arp_reply_callback(void *ml, void *buf, struct sockaddr_ll *addr,
			struct arp_protocol *p) {


}
int main(int argc, const char **argv) {
	return 0;
}
