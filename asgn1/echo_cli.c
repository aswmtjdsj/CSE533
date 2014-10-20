#include "common.h"

int main(int argc, char **argv) {
    /* variables declaration */
    int sock_fd, n, max_fd_p, stdin_eof;
    char buf[MAX_CHAR_LENGTH + 1];
    struct sockaddr_in server_address;

    fd_set f_set;

    char * input_param;

    /* input parsing */
    if (argc != 2) {
        err_quit("usage: ./time_cli <ip_addr>");
    }

    if(write( PIPE_ID, "[child] child process created\n", MAX_CHAR_LENGTH) < 0) {
        perror("pipe write error");
    }

    input_param = argv[1];

    /* initialization for SA structure */
    bzero(&server_address, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(ECHO_PORT_NUMBER); /* well-known time server port */

    /* get server host by ip */
    if (inet_pton(AF_INET, input_param, &server_address.sin_addr) <= 0) {
        if(write(PIPE_ID, "[child] inet_pton error\n", MAX_CHAR_LENGTH) < 0) {
            perror("pipe write error");
        }
        close(PIPE_ID);
        exit(1);
    }

    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        if(write( PIPE_ID, "[child] socket error\n", MAX_CHAR_LENGTH) < 0) {
            perror("pipe write error");
        }
        close(PIPE_ID);
        exit(1);
    }

    if (connect(sock_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        if(write( PIPE_ID, "[child] connect error\n", MAX_CHAR_LENGTH) < 0) {
            perror("pipe write error");
        }
        close(PIPE_ID);
        exit(1);
    }

    if(write( PIPE_ID, "[child] server connected\n", MAX_CHAR_LENGTH) < 0) {
        perror("pipe write error");
    }

    stdin_eof = 0;
    FD_ZERO(&f_set);

    for( ; ; ) {
        if(stdin_eof == 0) {
            FD_SET(STDIN_FILENO, &f_set);
        }
        FD_SET(sock_fd, &f_set);
        max_fd_p = ((sock_fd > STDIN_FILENO)?sock_fd:STDIN_FILENO) + 1;
        if(select(max_fd_p, &f_set, NULL, NULL, NULL) < 0) {
            if(errno == EINTR) {
                continue;
            }
            else {
                if(write( PIPE_ID, "[child] select error\n", MAX_CHAR_LENGTH) < 0) {
                    perror("pipe write error");
                }
            }
        }
        
        if(FD_ISSET(sock_fd, &f_set)) { /* read socket */
            if((n = read(sock_fd, buf, MAX_CHAR_LENGTH)) == 0) {
                if(stdin_eof == 1) { // 
                    if(write(PIPE_ID, "[child] ctrl-d or EOF detected, normal termination \n", MAX_CHAR_LENGTH) < 0) {
                        perror("pipe write error");
                    }
                }
                else {
                    if(write(PIPE_ID, "[child] server terminated prematurely\n", MAX_CHAR_LENGTH) < 0) {
                        perror("pipe write error");
                    }
                }
                close(PIPE_ID);
                exit(1);
            }
            if (fputs(buf, stdout) == EOF){ /* write received buf to stdout */
                if(write(PIPE_ID, "[child] fputs return EOF, fputs error\n", MAX_CHAR_LENGTH) < 0) {
                    perror("pipe write error");
                }
                close(PIPE_ID);
                exit(1);
            }
        }

        if(FD_ISSET(STDIN_FILENO, &f_set)) { /* handle stdin */
            if((n = read(STDIN_FILENO, buf, MAX_CHAR_LENGTH)) == 0) {
                stdin_eof = 1;
                shutdown(sock_fd, SHUT_WR);
                FD_CLR(STDIN_FILENO, &f_set);
                continue;
            }
            buf[n] = 0;
            if (write(sock_fd, buf, n) < 0){ /* write to server */
                if(write(PIPE_ID, "[child] write error\n", MAX_CHAR_LENGTH) < 0) {
                    perror("pipe write error");
                }
                close(PIPE_ID);
                exit(1);
            }
        }
    }

    return 0;
}
