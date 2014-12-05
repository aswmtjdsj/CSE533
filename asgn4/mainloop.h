#ifndef __MAINLOOP_H__
#define __MAINLOOP_H__
#include <sys/time.h>

#define FD_READ 1
#define FD_WRITE 2

typedef void (*timer_cb)(void *, void *, const struct timeval *);
typedef void (*fd_cb)(void *, void *, int);
void *timer_insert(void *, struct timeval *, timer_cb, void *data);
void *fd_insert(void *, int fd, int rw, fd_cb, void *data);
void fd_remove(void *, void *);
void timer_remove(void *, void *);
void *mainloop_new(void);
void mainloop_run(void *);
void timer_add(struct timeval *, const struct timeval *);
void timespec_substract(struct timespec *, const struct timespec *);
void timer_substract(struct timeval *, const struct timeval *);
void fd_set_cb(void *, fd_cb);
void fd_set_data(void *, void *);
int fd_get_fd(void *);
void *fd_get_data(void *);
void timer_set_cb(void *, timer_cb);
void *timer_get_data(void *);
void timer_set_data(void *, void *);
#endif
