#include "common.h"

static void * test_srv(void *);
static void *echo_thread(void * arg);
static void *time_thread(void * arg);

void
test_echo(int sock_fd)
{
    ssize_t n;
    char buf[MAX_CHAR_LENGTH + 1];
again:

    while ( (n = read(sock_fd, buf, MAX_CHAR_LENGTH)) > 0)
        writen(sock_fd, buf, n);
    if (n < 0 && errno == EINTR)
        goto again;
    else if (n < 0)
        err_sys("test_echo: read error");
}

void echo_func(int sock_fd) {
    int n, max_fd_p;
    char buf[MAX_CHAR_LENGTH + 1];
    fd_set f_set;

    FD_ZERO(&f_set);
    for( ; ; ) {
        FD_SET(sock_fd, &f_set);

        max_fd_p = sock_fd + 1;
        if((n = select(max_fd_p, &f_set, NULL, NULL, NULL)) < 0) { 
            /* only ctrl+c pressed, it has positive return values */
            if(errno == EINTR) { /* need */
                continue;
            }
            else {
                err_sys("select error");
            }
        }

        if(FD_ISSET(sock_fd, &f_set)) {
            if((n = readline(sock_fd, buf, MAX_CHAR_LENGTH)) == 0) {
                printf("Client termination: socket read returned with value 0\n");
                return ;
            }
            if(n < 0) {
                printf("Client termination: socket read returned with value %d\n", n);
                err_sys("read error");
            }
            buf[n] = 0;
            if(write(sock_fd, buf, n) < 0) {
                err_sys("write error");
            }
            printf("received from client: %s", buf);
        }
    }
}

void time_func(int sock_fd) {
    int n, max_fd_p;
    char buf[MAX_CHAR_LENGTH + 1];
    fd_set f_set;
    struct timeval time_val; /* for sleeping */
    time_t cur_time;

    time_val.tv_sec = 5;
    time_val.tv_usec = 0;

    FD_ZERO(&f_set);
    for( ; ; ) {
        FD_SET(sock_fd, &f_set);
        cur_time = time(NULL);

        snprintf(buf, sizeof(buf), "%.24s\n", ctime(&cur_time));
        int buf_len = strlen(buf);
        if(write(sock_fd, buf, buf_len) != buf_len) {
            err_sys("socket write error");
        }
        max_fd_p = sock_fd + 1;
        if((n = select(max_fd_p, &f_set, NULL, NULL, &time_val)) > 0) { 
            /* only ctrl+c pressed, it has positive return values */
            printf("Client termination: EPIPE error detected or ctrl-c pressed.\n");
            return ;
        } /* we cannot depend on select return value and errno in this only case */
    }
}

int main(int argc, char **argv) {
    /* variables declaration */
    int listen_fd[2], conn_fd, i, fd_flags, max_fd_p;
    socklen_t address_len;
    pthread_t thread_id;
    struct sockaddr_in client_addr, server_address[2];
    const int on_flag = 1;

    fd_set f_set;

    for(i = 0; i < 2; i++) { /* 0 for echo and 1 for time */
        /* init server addr */
        bzero(&server_address[i], sizeof(server_address[i]));
        server_address[i].sin_family = AF_INET;
        server_address[i].sin_addr.s_addr = htonl(INADDR_ANY);
        server_address[i].sin_port = htons(i==0?ECHO_PORT_NUMBER:TIME_PORT_NUMBER);

        /* create socket */
        if((listen_fd[i] = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            err_sys("socket error");
        }

        printf("Socket listened on port %d.\n", i==0?ECHO_PORT_NUMBER:TIME_PORT_NUMBER);
        if(setsockopt(listen_fd[i], SOL_SOCKET, SO_REUSEADDR, &on_flag, sizeof(on_flag))  < 0) {
            err_sys("set socket option error");
        }

        /* socket binding */
        if(bind(listen_fd[i], (struct sockaddr *) &server_address[i], sizeof(server_address[i])) < 0) {
            err_sys("bind error");
        }
        
        /* set non-blocking */
        if((fd_flags = fcntl(listen_fd[i], F_GETFL, 0)) < 0) {
            err_sys("F_GETFL error");
        }

        fd_flags |= O_NONBLOCK;

        if ((fcntl(listen_fd[i], F_SETFL, fd_flags)) < 0) {
            err_sys("F_SETFL error");
        }

        /* socket listening */
        if(listen(listen_fd[i], LISTENQ) < 0) {
            err_sys("listen error");
        }
    }

    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        err_sys("[parent] signal handler error");
    }

    FD_ZERO(&f_set);
    for(;;) {

        for(i = 0; i < 2; i++) {
            FD_SET(listen_fd[i], &f_set);
        }

        max_fd_p = ((listen_fd[0] > listen_fd[1])?listen_fd[0]:listen_fd[1]) + 1;
        
        if(select(max_fd_p, &f_set, NULL, NULL, NULL) < 0) {
            if(errno == EINTR) { /* need */
                continue;
            }
            else {
                err_sys("select error");
            }
        }

        address_len = sizeof(client_addr);
        for(i = 0; i < 2; i++) {
            if(FD_ISSET(listen_fd[i], &f_set)) {
                if((conn_fd = accept(listen_fd[i], (struct sockaddr *) &client_addr, &address_len)) < 0) {
                    close(listen_fd[i]);
                    if(errno == EINTR) {
                        continue;
                    }
                    else {
                        err_sys("accept error");
                    }
                }

                if(pthread_create(&thread_id, NULL, (i==0?(&echo_thread):(&time_thread)), (void *) conn_fd) < 0) {
                    err_sys("pthread error");
                }
            }
        }
    }
    
    return 0;
}

static void * test_srv(void * arg) {
    pthread_detach(pthread_self());
    test_echo((int) arg);
    close((int) arg);
    return NULL;
}

static void *echo_thread(void * arg) {
    printf("Echo Server Starting!\n");
    if(pthread_detach(pthread_self()) < 0) {
        err_sys("pthread_detach error");
    }
    echo_func((int) arg);
    if(close((int) arg) < 0) {
        err_sys("close fd error");
    }
    printf("Echo Server Closing!\n");
    return NULL;
}

static void *time_thread(void * arg) {
    printf("Time Server Starting!\n");
    if(pthread_detach(pthread_self()) < 0) {
        err_sys("pthread_detach error");
    }
    time_func((int) arg);
    if(close((int) arg) < 0) {
        err_sys("close fd error");
    }
    printf("Time Server Closing!\n");
    return NULL;
}
