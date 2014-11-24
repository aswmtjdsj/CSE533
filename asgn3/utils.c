#include <stddef.h>
#include <assert.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdarg.h>
#include "utils.h"
#include "log.h"

/* We can reuse a single socket fd */
static int sockfd = -1;

const char *sa_ntop(struct sockaddr *sa, char **dst, size_t *len) {
	int rlen;
	switch (sa->sa_family) {
		case AF_INET:
			rlen = INET_ADDRSTRLEN;
			break;
		case AF_INET6:
			rlen = INET6_ADDRSTRLEN;
			break;
		default:
			return NULL;
	}
	if (rlen > *len || !*dst) {
		free(*dst);
		*dst = malloc(rlen);
		*len = rlen;
	}
	switch (sa->sa_family) {
		case AF_INET:
			return inet_ntop(AF_INET,
			    &((struct sockaddr_in *)sa)->sin_addr, *dst, rlen);
		case AF_INET6:
			return inet_ntop(AF_INET6,
			    &((struct sockaddr_in6 *)sa)->sin6_addr, *dst,
			    rlen);
	}
	return NULL;
}

static void
_fill_ifi_info(struct ifreq *ifr, int flags, struct ifi_info *ifi) {
	assert(sockfd >= 0);
	void *sinptr;
	size_t len;
	ifi->ifi_flags = flags;
	memcpy(ifi->ifi_name, ifr->ifr_name, IFI_NAME);
	ifi->ifi_name[IFI_NAME-1] = '\0';
	switch (ifr->ifr_addr.sa_family) {
	case AF_INET:
		sinptr = (struct sockaddr_in *) &ifr->ifr_addr;
		ifi->ifi_addr = calloc(1, sizeof(struct sockaddr_in));
		len = sizeof(struct sockaddr_in);
		memcpy(ifi->ifi_addr, sinptr, sizeof(struct sockaddr_in));
		if (flags & IFF_BROADCAST) {
			if (!ioctl(sockfd, SIOCGIFBRDADDR, ifr)) {
				sinptr = (struct sockaddr_in *)
					     &ifr->ifr_broadaddr;
				ifi->ifi_brdaddr = calloc(1, len);
				memcpy(ifi->ifi_brdaddr, sinptr, len);
			}
		}
#ifdef	SIOCGIFDSTADDR
		if (flags & IFF_POINTOPOINT) {
			if (!ioctl(sockfd, SIOCGIFDSTADDR, ifr)) {
				sinptr = (struct sockaddr_in *)
					     &ifr->ifr_dstaddr;
				ifi->ifi_dstaddr = calloc(1, len);
				memcpy(ifi->ifi_dstaddr, sinptr, len);
			}
		}
#endif
		if (!ioctl(sockfd, SIOCGIFNETMASK, ifr)) {
			sinptr = (struct sockaddr_in *) &ifr->ifr_addr;
			ifi->ifi_ntmaddr = calloc(1, len);
			memcpy(ifi->ifi_ntmaddr, sinptr, len);
		}
			break;

	case AF_INET6:
		sinptr = (struct sockaddr_in6 *) &ifr->ifr_addr;
		len = sizeof(struct sockaddr_in6);
		ifi->ifi_addr = calloc(1, sizeof(struct sockaddr_in6));
		memcpy(ifi->ifi_addr, sinptr, sizeof(struct sockaddr_in6));
#ifdef	SIOCGIFDSTADDR
		if (flags & IFF_POINTOPOINT) {
			if (!ioctl(sockfd, SIOCGIFDSTADDR, ifr)) {
				sinptr = (struct sockaddr_in6 *)
					     &ifr->ifr_dstaddr;
				ifi->ifi_dstaddr = calloc(1, len);
				memcpy(ifi->ifi_dstaddr, sinptr, len);
			}
		}
#endif
		break;

	default:
		break;
	}

	ifi->ifi_mtu = 0;
	if (!ioctl(sockfd, SIOCGIFMTU, ifr))
		ifi->ifi_mtu = ifr->ifr_metric;
	if (!ioctl(sockfd, SIOCGIFHWADDR, ifr)) {
		memcpy(ifi->ifi_hwaddr, ifr->ifr_hwaddr.sa_data, IFHWADDRLEN);
		ifi->ifi_halen = IFHWADDRLEN;
	} else
		log_err("Failed to get hardware address for %s\n",
		    ifr->ifr_name);
	if (!ioctl(sockfd, SIOCGIFINDEX, ifr))
		ifi->ifi_index = ifr->ifr_ifindex;
	else
		log_err("Failed to get index for %s\n", ifr->ifr_name);
}

struct ifi_info *
get_ifi_info(int family, int doaliases) {
	struct ifi_info *ifi, *ifihead, **ifipnext;
	int myflags, idx = 0;
	char *ptr, *buf, lastname[IFNAMSIZ];
	struct ifconf ifc;
	struct ifreq *ifr, ifrcopy;

	if (sockfd < 0) {
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sockfd < 0)
			return NULL;
	}

	ifc.ifc_req = NULL;
	ifc.ifc_len = 0;
	/* initial buffer size guess */
	if (ioctl(sockfd, SIOCGIFCONF, &ifc) < 0) {
		log_err("ioctl failed: %s\n", strerror(errno));
		return NULL;
	}
	buf = ifc.ifc_buf = malloc(ifc.ifc_len);
	if (ioctl(sockfd, SIOCGIFCONF, &ifc) < 0) {
		log_err("ioctl failed: %s\n", strerror(errno));
		return NULL;
	}

	ifihead = NULL;
	ifipnext = &ifihead;
	lastname[0] = 0;

	for (ptr = buf; ptr < buf + ifc.ifc_len; ) {
		ifr = (struct ifreq *) ptr;
		/* for next one in buffer */
		ptr = (char *)(((struct ifreq *)ptr)+1);

		if (ifr->ifr_addr.sa_family != family)
			continue;

		myflags = 0;
		if (strncmp(lastname, ifr->ifr_name, IFNAMSIZ) == 0) {
			/* already processed this interface */
			if (doaliases == 0)
				continue;
			myflags = IFI_ALIAS;
		}
		memcpy(lastname, ifr->ifr_name, IFNAMSIZ);

		ifrcopy = *ifr;
		if (ioctl(sockfd, SIOCGIFFLAGS, &ifrcopy) < 0)
			/* Can't get flags, ignore this one */
			continue;

		int flags = ifrcopy.ifr_flags;
		if ((flags & IFF_UP) == 0)
			continue;

		ifi = calloc(1, sizeof(struct ifi_info));
		*ifipnext = ifi;
		ifipnext = &ifi->ifi_next;

		ifi->ifi_index = idx;
		ifi->ifi_myflags = myflags;

		ifrcopy = *ifr;
		_fill_ifi_info(&ifrcopy, flags, ifi);
	}
	free(buf);
	return ifihead;
}

void
free_ifi_info(struct ifi_info *ifihead)
{
	struct ifi_info	*ifi, *ifinext;

	for (ifi = ifihead; ifi != NULL; ifi = ifinext) {
		if (ifi->ifi_addr != NULL)
			free(ifi->ifi_addr);
		if (ifi->ifi_brdaddr != NULL)
			free(ifi->ifi_brdaddr);
		if (ifi->ifi_dstaddr != NULL)
			free(ifi->ifi_dstaddr);
		if (ifi->ifi_ntmaddr != NULL)
			free(ifi->ifi_ntmaddr);

		ifinext = ifi->ifi_next;
		free(ifi);
	}
}

void dump_ifi_info(int family, int doaliases) {
	struct ifi_info *ifihead = get_ifi_info(family, doaliases), *ifi;
	for (ifi = ifihead; ifi != NULL; ifi = ifi->ifi_next) {
		log_info("%s: ", ifi->ifi_name);
		if (ifi->ifi_index != 0)
			printf("(%d) ", ifi->ifi_index);
		log_info("<");
		if (ifi->ifi_flags & IFF_UP)
			log_info("UP ");
		if (ifi->ifi_flags & IFF_BROADCAST)
			log_info("BCAST ");
		if (ifi->ifi_flags & IFF_MULTICAST)
			log_info("MCAST ");
		if (ifi->ifi_flags & IFF_LOOPBACK)
			log_info("LOOP ");
		if (ifi->ifi_flags & IFF_POINTOPOINT)
			log_info("P2P ");
		log_info("\b>\n");

		if (ifi->ifi_mtu != 0)
			log_info("\tMTU: %d\n", ifi->ifi_mtu);

		char *tmp = NULL;
		size_t len = 0;
		struct sockaddr *sa;
		if ((sa = ifi->ifi_addr) != NULL)
			log_info("\tIP addr: %s\n", sa_ntop(sa, &tmp, &len));

		if ((sa = ifi->ifi_ntmaddr) != NULL)
			log_info("\tnetwork mask: %s\n",
			    sa_ntop(sa, &tmp, &len));

		if ((sa = ifi->ifi_brdaddr) != NULL)
			log_info("\tbroadcast addr: %s\n",
			    sa_ntop(sa, &tmp, &len));
		if ((sa = ifi->ifi_dstaddr) != NULL)
			log_info("\tdestination addr: %s\n",
			    sa_ntop(sa, &tmp, &len));
	}
	free_ifi_info(ifihead);
}

void my_err_quit(const char * prompt) {
    log_err("%s: %s\n", prompt, strerror(errno));
    exit(EXIT_FAILURE);
}
/* vim: set noexpandtab tabstop=8: */
