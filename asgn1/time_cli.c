#include "common.h"

int main(int argc, char **argv) {
    /* variables declaration */
    int sock_fd, n;
    char recv_line[MAX_CHAR_LENGTH + 1], buf[MAX_CHAR_LENGTH+1];
    struct sockaddr_in server_address;

    char * input_param;

    /* input parsing */
    if (argc != 2) {
        err_quit("[child] usage: ./time_cli <ip_addr>");
    }

    if(write( PIPE_ID, "[child] child process created\n", MAX_CHAR_LENGTH) < 0) {
        perror("pipe write error");
    }

    input_param = argv[1];

    /* initialization for SA structure */
    bzero(&server_address, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(TIME_PORT_NUMBER); /* well-known time server port */

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

    while ( (n = read(sock_fd, recv_line, MAX_CHAR_LENGTH)) > 0){
        recv_line[n] = 0;
        if (fputs(recv_line, stdout) == EOF){
            if(write(PIPE_ID, "[child] fputs return EOF, fputs error\n", MAX_CHAR_LENGTH) < 0) {
                perror("pipe write error");
            }
            close(PIPE_ID);
            exit(1);
        }
    }

    if( n == 0) {
        if(write(PIPE_ID, "[child] server read return value = 0, time_cli: server terminated prematurely\n", MAX_CHAR_LENGTH) < 0) {
            perror("pipe write error");
        }
        close(PIPE_ID);
        exit(1);
    }

    if (n < 0){
        if(write(PIPE_ID, "[child] server read return value < 0, read error\n", MAX_CHAR_LENGTH) < 0) {
            perror("pipe write error");
        }
        close(PIPE_ID);
        exit(1);
    }

    return 0;
}
