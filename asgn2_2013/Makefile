
CC = gcc

LIBS = -lm -lresolv -lsocket -lnsl -lpthread\
	   /home/courses/cse533/Asgn2_code/get_ifi_info_plus.o\
	   /home/courses/cse533/Stevens/unpv13e_solaris2.10/libunp.a\
	    		
FLAGS = -g -O2

CFLAGS = ${FLAGS} -I/home/courses/cse533/Stevens/unpv13e_solaris2.10/lib -I/home/courses/cse533/Asgn2_code/

OBJ= udp_client.o udp_server.o 

ALL= client server 

all: client server 

# server uses the thread-safe version of readline.c

server: udp_server.o
	${CC} ${FLAGS} -o $@ $^ ${LIBS}

udp_server.o: udp_server.c
	${CC} ${CFLAGS} -c $^ 

client: udp_client.o
	${CC} ${FLAGS} -o $@ $^ ${LIBS}

udp_client.o: udp_client.c
	${CC} ${CFLAGS} -c $^

# pick up the thread-safe version of readline.c from directory "threads"

readline.o: /home/courses/cse533/Stevens/unpv13e_solaris2.10/threads/readline.c
	${CC} ${CFLAGS} -c /home/courses/cse533/Stevens/unpv13e_solaris2.10/threads/readline.c

clean:
	rm $(OBJ) $(ALL)
