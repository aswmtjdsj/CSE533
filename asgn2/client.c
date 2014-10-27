#include <sys/time.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "mainloop.h"
void timer_callback(void *ml, void *data, const struct timeval *elapsed) {
	double e = elapsed->tv_sec;
	struct timeval *xx = data;
	double e2 = xx->tv_sec;
	e2 += ((double)xx->tv_usec)/1e6;
	e += ((double)elapsed->tv_usec)/1e6;
	fprintf(stderr, "expected %lf, real %lf\n", e2, e);
	free(data);
}
int main(int argc, char * const *argv) {
	void *ml = mainloop_new();
	struct timeval *tv = malloc(sizeof(struct timeval));
	tv->tv_sec = 2;
	tv->tv_usec = 2000;
	timer_insert(ml, tv, timer_callback, tv);
	tv = malloc(sizeof(struct timeval));
	tv->tv_sec = 2;
	tv->tv_usec = 4100;
	timer_insert(ml, tv, timer_callback, tv);
	mainloop_run(ml);
	free(ml);
}
