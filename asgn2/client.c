#include <sys/time.h>
#include <stddef.h>
#include <stdio.h>
#include "mainloop.h"
void timer_callback(void *ml, void *data, const struct timeval *elapsed) {
	double e = elapsed->tv_sec;
	e += ((double)elapsed->tv_usec)/1e6;
	fprintf(stderr, "expected 2.002, real %lf\n", e);
}
int main(int argc, char * const *argv) {
	void *ml = mainloop_new();
	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 2000;
	timer_insert(ml, &tv, timer_callback, NULL);
	mainloop_run(ml);
}
