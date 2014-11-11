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
	int len, lastlen, myflags, idx = 0;
	char *ptr, *buf, lastname[IFNAMSIZ];
	struct ifconf ifc;
	struct ifreq *ifr, ifrcopy;

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

void info_print(const char * fmt, ...) {
#ifdef ENABLE_COLOR
    printf("\033[32m");
#endif
    printf("[INFO] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#ifdef ENABLE_COLOR
    printf("\033[0m");
#endif
}

void warn_print(const char * fmt, ...) { 
#ifdef ENABLE_COLOR
    printf("\033[33m");
#endif
    printf("[WARNING] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#ifdef ENABLE_COLOR
    printf("\033[0m");
#endif
}

void err_print(const char * fmt, ...) { 
#ifdef ENABLE_COLOR
    printf("\033[31m");
#endif
    printf("[ERROR] ");
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#ifdef ENABLE_COLOR
    printf("\033[0m");
#endif
}

void my_err_quit(const char * prompt) {
    err_print("%s: %s\n", prompt, strerror(errno));
    exit(EXIT_FAILURE);
}
/* vim: set noexpandtab tabstop=8: */
