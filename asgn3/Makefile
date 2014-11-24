CC = gcc
LIBS = -lrt
time_client_OBJS = client.o utils.o msg_api.o
time_server_OBJS = server.o utils.o msg_api.o
odr_server_OBJS = odr_server.o mainloop.o utils.o odr_protocol.o msg_api.o
OBJS = $(time_client_OBJS) $(time_server_OBJS) $(odr_server_OBJS)
ID = syuxuan
BINARIES = odr_server_$(ID) time_client_$(ID) time_server_$(ID)
CFLAGS = -g -Wall -Wno-unused-function

all: $(OBJS) $(BINARIES)

%.P : %.c
	gcc -MM -MT $(<:.c=.o) -MT $(<:.c=.P) -MF $@ $<

-include $(OBJS:.o=.P)

time_client_$(ID): $(time_client_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

time_server_$(ID): $(time_server_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

odr_server_$(ID): $(odr_server_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm $(BINARIES) $(OBJS) $(OBJS:.o=.P)
