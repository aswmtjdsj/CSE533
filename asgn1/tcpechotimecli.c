#include "common.h"

void test_cli(FILE * fp, int sock_fd) {
    char send_line[MAX_CHAR_LENGTH], recv_line[MAX_CHAR_LENGTH];
    while(fgets(send_line, MAX_CHAR_LENGTH, fp) != NULL) {
        writen(sock_fd, send_line, strlen(send_line));

        if(readline(sock_fd, recv_line, MAX_CHAR_LENGTH) == 0) {
            err_quit("[parent] test_cli: server terminated prematurely");
        }
        fputs(recv_line, stdout);
    }
}

void sig_child(int signo) {
    pid_t pid;
    int stat;
    pid = wait(&stat);
    printf("[parent] child process %d terminated\n", pid);
    return ;
}

int main(int argc, char **argv) {
    /* variables declaration */
    int sock_fd, n, comm_flag = 0;
    char recv_line[MAX_CHAR_LENGTH + 1], command[MAX_CHAR_LENGTH + 1];
    struct sockaddr_in server_address;

    struct hostent * server_host;
    char * input_param, ** addr_pointer, addr[MAX_CHAR_LENGTH + 1];

    int process_id;
    int pipe_fd[2];

    /* input parsing */
    if (argc != 2)
        err_quit("[parent] usage: ./client <ip_addr> | <host_name>");

    input_param = argv[1];

    /* initialization for SA structure */
    bzero(&server_address, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(ECHO_PORT_NUMBER); /* well-known time server port */

    /* get server host by name or addr */
    if (inet_pton(AF_INET, input_param, &server_address.sin_addr) <= 0) {

        /* probably host */
        if((server_host = gethostbyname(input_param)) == NULL) {
            err_msg ("[parent] gethostbyname error for host [%s]: %s",
                    input_param, hstrerror (h_errno) );
            err_quit("[parent] wrong input, quit!");
        }
        printf ("[parent] The server host is [%s].\n", server_host->h_name);

        /* enumerate addr associated with host */
        switch (server_host->h_addrtype) {
            case AF_INET:
                addr_pointer = server_host->h_addr_list;
                for ( ; *addr_pointer != NULL; addr_pointer++) {
                    inet_ntop(server_host->h_addrtype, *addr_pointer, addr, sizeof (addr));
                }
                break;
            default:
                err_ret ("[parent] unknown address type");
                break;
        }

        /* convert addr 'p' to 'n' and assign it to server_address struct */
        if (inet_pton(AF_INET, addr, &server_address.sin_addr) <= 0) {
            err_quit("[parent] inet_pton error for %s", addr);
        }
        else {
            printf ("[parent] The server address is: %s\n", addr);
        }
    }
    else {
        /* definitely addr, no matter correct or not */
        inet_ntop(AF_INET, &server_address.sin_addr, addr, sizeof (addr));
        printf ("[parent] The server address is: %s\n", addr);
    }

    if(signal(SIGCHLD, sig_child) == SIG_ERR || signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        err_sys("[parent] signal handler error");
    }

    for(;;) {
        if(fgets(command, MAX_CHAR_LENGTH, stdin) == NULL) {
            err_sys("[parent] error detected or EOF");
        }
        if(strncmp(command, "echo", 4) == 0) {
            comm_flag = 1;
        }
        else if(strncmp(command, "time", 4) == 0) {
            comm_flag = 2;
        }
        else if(strncmp(command, "quit", 4) == 0) {
            comm_flag = 3;
        }
        else {
            comm_flag = -1;
            printf("[parent] command should be [echo] or [time] or [quit] (case sensitive)! Please re-input!\n");
            continue;
        }
        if(comm_flag == 3) {
            /* need to kill xterm */
            printf("[parent] quit command detected, client terminated.\n");
            break;
        }

        /* if valid input */
        if(comm_flag > 0) {
            printf("[parent] forking child for %s.\n", comm_flag==1?"echo":"time");
            if(pipe(pipe_fd) == -1) {
                err_sys("[parent] pipe failed");
            }
            if((process_id = fork()) < 0) {
                err_sys("[parent] fork failed");
            }
            /* child process */
            if(process_id == 0) {
                close(pipe_fd[0]); /* read from stdin */
                dup2(pipe_fd[1], PIPE_ID); /* to re-write to parent */ 
                close(pipe_fd[1]);
                if(comm_flag == 1) {
                    printf("[child] calling ./echo_cli to connect %s.\n", addr);
                    execlp("xterm", "xterm", "-e", "./echo_cli", addr, (char *) 0);
                }
                else if(comm_flag == 2) {
                    printf("[child] calling ./time_cli to connect %s.\n", addr);
                    execlp("xterm", "xterm", "-e", "./time_cli", addr, (char *) 0);
                }
                close(PIPE_ID);
            }
            /* parent process */
            else {
                close(pipe_fd[1]); /* write to stdout */
                for(;;) {
                    while ( (n = read(pipe_fd[0], recv_line, MAX_CHAR_LENGTH)) > 0) {
                        recv_line[n] = 0; // terminate str
                        if(fputs(recv_line, stdout) == EOF)
                            err_sys("[parent] fputs return EOF, fputs error");
                    }
                    if(n == 0) {
                        printf("[parent] xterm read value = 0, xterm terminated\n");
                        break;
                    }
                    if(n < 0) {
                        err_sys("[parent] xterm read < 0, xterm read error");
                    }
                }
                close(pipe_fd[0]);
            }
        }
    }

    return 0;
}
