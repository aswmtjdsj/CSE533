#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "mainloop.h"
struct mainloop {
	struct timer *timers;
	struct fd *fds;
	int maxfd;
};
struct timer {
	struct timeval tv;
	struct timeval realtv;
	void *data;
	timer_cb cb;
	struct timer *next;
};
struct fd {
	int fd;
	void *data;
	fd_cb cb;
	int rw;
	struct fd *next;
};

static
void timer_substract(struct timeval *tv1, struct timeval *tv2) {
	tv1->tv_sec -= tv2->tv_sec;
	tv1->tv_usec -= tv2->tv_usec;
	if (tv1->tv_usec < 0) {
		tv1->tv_sec--;
		tv1->tv_usec+=1000000;
	}
}
static
int timer_cmp(struct timeval *a, struct timeval *b) {
	if (a->tv_sec < b->tv_sec)
		return -1;
	if (a->tv_sec > b->tv_sec)
		return 1;
	if (a->tv_usec < b->tv_usec)
		return -1;
	if (a->tv_usec > b->tv_usec)
		return 1;
	return 0;
}
#define next_timer(head) {.tv_sec = head->sec, .tv_usec = head->usec}
void
timer_insert(void *loop, struct timeval *tv, timer_cb cb, void *data) {
	struct mainloop *ml = loop;
	struct timer *tmp = ml->timers;
	struct timer **nextp = &ml->timers;
	struct timeval ttv = *tv;
	while(1){
		if (timer_cmp(&ttv, &tmp->tv) < 0)
			break;
		timer_substract(&ttv, &tmp->tv);
		nextp = &tmp->next;
		tmp = tmp->next;
	}
	struct timer *nt = malloc(sizeof(struct timer));
	nt->tv = ttv;
	nt->next = tmp;
	nt->cb = cb;
	nt->data = data;
	timer_substract(&tmp->tv, &ttv);
}
void fd_insert(void *loop, int fd, int rw, fd_cb cb, void *data) {
	struct mainloop *ml = loop;
	struct fd *nfd = malloc(sizeof(struct fd));
	nfd->fd = fd;
	nfd->data = data;
	nfd->cb = cb;
	nfd->rw = rw;
	nfd->next = ml->fds;
	ml->fds = nfd;
	if (fd > ml->maxfd)
		ml->maxfd = fd;
}

static void
recalc_maxfd(struct mainloop *ml) {
	struct fd *tmp = ml->fds;
	ml->maxfd = -1;
	while(tmp) {
		if (tmp->fd > ml->maxfd)
			ml->maxfd = tmp->fd;
		tmp = tmp->next;
	}
}

void fd_remove(void *loop, int fd) {
	struct mainloop *ml = loop;
	struct fd **nextp = &ml->fds;
	struct fd *tfd = ml->fds;
	while(tfd) {
		if (tfd->fd == fd) {
			*nextp = tfd->next;
			free(tfd);
			recalc_maxfd(ml);
			return;
		}
		nextp = &tfd->next;
		tfd = tfd->next;
	}
}

static
void timer_elapse(struct mainloop *ml, struct timeval *elapse) {
	struct timer *tt = ml->timers;
	while(1) {
		struct timer *nt = tt->next;
		struct timeval realtv;
		if (timer_cmp(&tt->tv, elapse) > 0) {
			timer_substract(&tt->tv, elapse);
			break;
		}
		if (tt->cb) {
			realtv = *elapse;
			timer_substract(&realtv, &tt->tv);
			realtv.tv_sec += tt->realtv.tv_sec;
			realtv.tv_usec += tt->realtv.tv_usec;
			tt->cb(ml, tt->data, &realtv);
		}
		timer_substract(elapse, &tt->tv);
		free(tt);
		tt = nt;
		if (!tt)
			break;
	}
	ml->timers = tt;
}

void mainloop(void *data) {
	struct mainloop *ml = data;
	struct timeval lasttv;
	fd_set rfds, wfds;

	gettimeofday(&lasttv, NULL);
	while(ml->fds || ml->timers) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		struct fd *tfd = ml->fds;
		while(tfd) {
			if (tfd->rw & 1)
				FD_SET(tfd->fd, &rfds);
			if (tfd->rw & 2)
				FD_SET(tfd->fd, &wfds);
		}
		struct timeval nowtv, ntv = ml->timers->tv;
		gettimeofday(&nowtv, NULL);
		timer_substract(&nowtv, &lasttv);
		timer_substract(&ntv, &nowtv);
		int ret = select(ml->maxfd, &rfds, &wfds, NULL, &ntv);
		gettimeofday(&nowtv, NULL);
		timer_substract(&nowtv, &lasttv);
		gettimeofday(&lasttv, NULL);
		timer_elapse(ml, &nowtv);
		if (ret <= 0)
			continue;
		tfd = ml->fds;
		while(tfd) {
			int rw = 0;
			if ((tfd->rw & 1) && FD_ISSET(tfd->fd, &rfds))
				rw |= 1;
			if ((tfd->rw & 2) && FD_ISSET(tfd->fd, &wfds))
				rw |= 2;
			if (rw && tfd->cb)
				tfd->cb(ml, tfd->data, rw);
		}
	}
}
void *mainloop_new(void){
	struct mainloop *ml = malloc(sizeof(struct mainloop));
	ml->fds = NULL;
	ml->timers = NULL;
	ml->maxfd = -1;
	return ml;
}
