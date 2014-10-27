#ifndef __MAINLOOP_H__
#define __MAINLOOP_H__
#include <sys/time.h>
typedef void (*timer_cb)(void *, void *, const struct timeval *);
typedef void (*fd_cb)(void *, void *, int);
void timer_insert(void *, struct timeval *, timer_cb, void *data);
void fd_insert(void *, int fd, int rw, fd_cb, void *data);
void fd_remove(void *, int fd);
void *mainloop_new(void);
void mainloop_run(void *);
#endif
