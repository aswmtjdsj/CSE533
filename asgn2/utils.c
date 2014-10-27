#include <stddef.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <assert.h>
#include "utils.h"

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
_fill_ifi_info(struct ifreq *ifr, struct ifi_info *ifi) {
	assert(sockfd >= 0);
	void *sinptr;
	int flags = ifr->ifr_flags;
	int len;
	ifi->ifi_flags = flags;
	memcpy(ifi->ifi_name, ifr->ifr_name, IFI_NAME);
	ifi->ifi_name[IFI_NAME-1] = '\0';
	switch (ifr->ifr_addr.sa_family) {
	case AF_INET:
		sinptr = (struct sockaddr_in *) &ifr->ifr_addr;
		ifi->ifi_addr = calloc(1, sizeof(struct sockaddr_in));
		len = sizeof(struct sockaddr_in);
		memcpy(ifi->ifi_addr, sinptr, sizeof(struct sockaddr_in));
#ifdef	SIOCGIFBRDADDR
		if (flags & IFF_BROADCAST) {
			if (!ioctl(sockfd, SIOCGIFBRDADDR, ifr)) {
				sinptr = (struct sockaddr_in *)
					     &ifr->ifr_broadaddr;
				ifi->ifi_brdaddr = calloc(1, len);
				memcpy(ifi->ifi_brdaddr, sinptr, len);
			}
		}
#endif
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
#ifdef  SIOCGIFNETMASK
		if (!ioctl(sockfd, SIOCGIFNETMASK, ifr)) {
			sinptr = (struct sockaddr_in *) &ifr->ifr_addr;
			ifi->ifi_ntmaddr = calloc(1, len);
			memcpy(ifi->ifi_ntmaddr, sinptr, len);
		}
#endif
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
#if defined(SIOCGIFMTU)
	if (!ioctl(sockfd, SIOCGIFMTU, ifr))
		ifi->ifi_mtu = ifr->ifr_metric;
#endif
}

struct ifi_info *
get_ifi_info(int family, int doaliases) {
	struct ifi_info *ifi, *ifihead, **ifipnext;
	int len, lastlen, flags, myflags, idx = 0;
	char *ptr, *buf, lastname[IFNAMSIZ], *cptr, *haddr;
	struct ifconf ifc;
	struct ifreq *ifr, ifrcopy;
	struct sockaddr_in *sinptr;
	struct sockaddr_in6 *sin6ptr;

	if (sockfd < 0) {
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sockfd < 0)
			return NULL;
	}

	lastlen = 0;
	/* initial buffer size guess */
	len = 100 * sizeof(struct ifreq);
	for ( ; ; ) {
		buf = malloc(len);
		ifc.ifc_len = len;
		ifc.ifc_buf = buf;
		if (ioctl(sockfd, SIOCGIFCONF, &ifc) < 0) {
			if (errno != EINVAL || lastlen != 0)
				return NULL;
		} else {
			if (ifc.ifc_len == lastlen)
				/* len doesn't change */
				break;
			lastlen = ifc.ifc_len;
		}
		len += 10 * sizeof(struct ifreq);
		free(buf);
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

		flags = ifrcopy.ifr_flags;
		if ((flags & IFF_UP) == 0)
			continue;

		ifi = calloc(1, sizeof(struct ifi_info));
		*ifipnext = ifi;
		ifipnext = &ifi->ifi_next;

		ifrcopy = *ifr;
		ifi->ifi_index = idx;
		ifi->ifi_myflags = myflags;
		_fill_ifi_info(&ifrcopy, ifi);
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

int
check_address(struct sock_info_aux * host_addr, struct sock_info_aux * cur_ip_addr) {
    char * tmp = NULL;
    size_t addr_len = 0;
    if( strcmp(sa_ntop(host_addr->ip_addr, &tmp, &addr_len), LOOP_BACK_ADDR) == 0 
            || ((struct sockaddr_in *) host_addr->ip_addr)->sin_addr.s_addr == ((struct sockaddr_in *) cur_ip_addr->ip_addr)->sin_addr.s_addr
            ) {
        return FLAG_LOOP_BACK;
    }
    else if(((struct sockaddr_in *) host_addr->subn_addr)->sin_addr.s_addr == ((struct sockaddr_in *)cur_ip_addr->subn_addr)->sin_addr.s_addr
            || (((struct sockaddr_in *) host_addr->ip_addr)->sin_addr.s_addr & ((struct sockaddr_in *) host_addr->net_mask)->sin_addr.s_addr) 
             == (((struct sockaddr_in *) cur_ip_addr->ip_addr)->sin_addr.s_addr & ((struct sockaddr_in *)cur_ip_addr->net_mask)->sin_addr.s_addr)) {
        return FLAG_LOCAL;
    }
    else {
        return FLAG_NON_LOCAL;
    }
}
