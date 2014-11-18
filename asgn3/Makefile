CC = gcc
LIBS = -lrt
log_server_OBJS = log_server.o mainloop.o utils.o
cmd_server_OBJS = cmd_server.o mainloop.o utils.o
client_OBJS = client.o utils.o
odr_server_OBJS = odr_server.o
OBJS = $(log_server_OBJS) $(cmd_server_OBJS) $(client_OBJS) $(odr_server_OBJS)
BINARIES = log_server odr_server cmd_server client
CFLAGS = -g -DLOG_LEVEL=10 -Wall -Wno-unused-function

all: $(OBJS) $(BINARIES)

%.P : %.c
	gcc -MM -MT $(<:.c=.o) -MT $(<:.c=.P) -MF $@ $<

-include $(OBJS:.o=.P)

log_server: $(log_server_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

cmd_server: $(cmd_server_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

client: $(client_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

odr_server: $(odr_server_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm $(BINARIES) $(OBJS) $(OBJS:.o=.P)
