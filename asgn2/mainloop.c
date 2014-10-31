#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "log.h"
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

#define	TIMEVAL_TO_TIMESPEC(tv, ts) {					\
	(ts)->tv_sec = (tv)->tv_sec;					\
	(ts)->tv_nsec = (tv)->tv_usec * 1000;				\
}
#define	TIMESPEC_TO_TIMEVAL(tv, ts) {					\
	(tv)->tv_sec = (ts)->tv_sec;					\
	(tv)->tv_usec = (ts)->tv_nsec / 1000;				\
}

void timespec_substract(struct timespec *t1, const struct timespec *t2) {
	t1->tv_sec -= t2->tv_sec;
	t1->tv_nsec -= t2->tv_nsec;
	if (t1->tv_nsec < 0) {
		t1->tv_sec--;
		t1->tv_nsec+=1000000000;
	}
}
void timer_substract(struct timeval *tv1, const struct timeval *tv2) {
	tv1->tv_sec -= tv2->tv_sec;
	tv1->tv_usec -= tv2->tv_usec;
	if (tv1->tv_usec < 0) {
		tv1->tv_sec--;
		tv1->tv_usec+=1000000;
	}
}
void timer_add(struct timeval *tv1, const struct timeval *tv2) {
	tv1->tv_sec += tv2->tv_sec;
	tv1->tv_usec += tv2->tv_usec;
	if (tv1->tv_usec >= 1000000) {
		tv1->tv_sec++;
		tv1->tv_usec-=1000000;
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
void *
timer_insert(void *loop, struct timeval *tv, timer_cb cb, void *data) {
	struct mainloop *ml = loop;
	struct timer *tmp = ml->timers;
	struct timer **nextp = &ml->timers;
	struct timeval ttv = *tv;
	while(1){
		if (!tmp || timer_cmp(&ttv, &tmp->tv) < 0)
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
	nt->realtv = *tv;
	*nextp = nt;
	if (tmp)
		timer_substract(&tmp->tv, &ttv);
	return nt;
}
void *fd_insert(void *loop, int fd, int rw, fd_cb cb, void *data) {
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
	return nfd;
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

void fd_remove(void *loop, void *handle) {
	struct mainloop *ml = loop;
	struct fd **nextp = &ml->fds;
	struct fd *tfd = ml->fds;
	while(tfd) {
		if (tfd == handle) {
			*nextp = tfd->next;
			free(tfd);
			recalc_maxfd(ml);
			return;
		}
		nextp = &tfd->next;
		tfd = tfd->next;
	}
}

void timer_remove(void *loop, void *handle) {
	struct mainloop *ml = loop;
	struct timer **nextp = &ml->timers;
	struct timer *tt = ml->timers;
	while(tt) {
		if (tt == handle) {
			*nextp = tt->next;
			if (tt->next)
				timer_add(&tt->next->tv, &tt->tv);
			free(tt);
			return;
		}
		nextp = &tt->next;
		tt = tt->next;
	}
}

static
void timer_elapse(struct mainloop *ml, struct timespec *_elapse) {
	log_debug("Elapse start\n");
	struct timer *tt = ml->timers;
	struct timespec start, end;
	struct timeval elapse;
	TIMESPEC_TO_TIMEVAL(&elapse, _elapse);
	int count = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	while(1) {
		struct timeval realtv;
		if (timer_cmp(&tt->tv, &elapse) > 0) {
			timer_substract(&tt->tv, &elapse);
			break;
		}
		ml->timers = tt->next;
		if (tt->cb) {
			realtv = elapse;
			timer_substract(&realtv, &tt->tv);
			timer_add(&realtv, &tt->realtv);
			tt->cb(ml, tt->data, &realtv);
		}
		count++;
		if (!(count & 255))
			log_warning("More than %d timer event fired at once,"
				"this could potentially cause starve!!\n", count);
		clock_gettime(CLOCK_MONOTONIC, &end);
		timespec_substract(&end, &start);
		TIMESPEC_TO_TIMEVAL(&realtv, &end);
		timer_add(&elapse, &realtv);
		timer_substract(&elapse, &tt->tv);
		free(tt);
		tt = ml->timers;
		if (!tt)
			break;
	}
	log_debug("Elapse end\n");
}

void mainloop_run(void *data) {
	struct mainloop *ml = data;
	struct timespec end;
	fd_set rfds, wfds;

	clock_gettime(CLOCK_MONOTONIC, &end);
	while(ml->fds || ml->timers) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		struct fd *tfd = ml->fds;
		while(tfd) {
			if (tfd->rw & 1)
				FD_SET(tfd->fd, &rfds);
			if (tfd->rw & 2)
				FD_SET(tfd->fd, &wfds);
			tfd = tfd->next;
		}

		struct timespec nowh;
		struct timeval nowl, nto, *ntop = NULL;
		if (ml->timers) {
			nto = ml->timers->tv;
			ntop = &nto;
			clock_gettime(CLOCK_MONOTONIC, &nowh);
			timespec_substract(&nowh, &end);
			TIMESPEC_TO_TIMEVAL(&nowl, &nowh);
			timer_substract(&nto, &nowl);
		}

		int ret = select(ml->maxfd+1, &rfds, &wfds, NULL, ntop);

		if (ml->timers) {
			clock_gettime(CLOCK_MONOTONIC, &nowh);
			timespec_substract(&nowh, &end);
			timer_elapse(ml, &nowh);
		}

		clock_gettime(CLOCK_MONOTONIC, &end);
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
			tfd = tfd->next;
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

void fd_set_cb(void *fh, fd_cb cb){
	struct fd *fd = fh;
	fd->cb = cb;
}

void timer_set_cb(void *th, timer_cb cb){
	struct timer *t = th;
	t->cb = cb;
}
