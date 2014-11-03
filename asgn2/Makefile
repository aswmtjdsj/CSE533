CC = gcc
LIBS = 
prifinfo_plus_OBJS = utils.o prifinfo_plus.o
client_OBJS = client.o mainloop.o protocol.o utils.o
client_LIBS = -lm -lrt
server_OBJS = server.o utils.o
OBJS = $(prifinfo_plus_OBJS) $(client_OBJS) $(server_OBJS)
UNAME := $(shell uname -s)
CFLAGS = -g -DLOG_LEVEL=10 -Wall -Wno-unused-function
ifeq ($(UNAME),SunOS)
	LIBS += -lnsl -lsocket -lresolv
	client_CFLAGS += -D_REENTRANT
	client_LIBS += -lpthread
	CFLAGS += -DBSD_COMP -DRANDOM_MAX=2147483647
else
	client_CFLAGS += -pthread
	CFLAGS += -DRANDOM_MAX=RAND_MAX
endif

all: $(OBJS) prifinfo_plus client server

%.P : %.c
	gcc -MM -MT $(<:.c=.o) -MT $(<:.c=.P) -MF $@ $<

-include $(OBJS:.o=.P)

$(client_OBJS): CFLAGS+=$(client_CFLAGS)

prifinfo_plus: $(prifinfo_plus_OBJS)
	$(CC) $(CFLAGS) -o prifinfo_plus $(prifinfo_plus_OBJS) $(LIBS)

client: $(client_OBJS)
	$(CC) $(CFLAGS) -o client $(client_OBJS) $(client_LIBS) $(client_CFLAGS) $(LIBS)

server: $(server_OBJS)
	$(CC) $(CFLAGS) -o server $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm prifinfo_plus client server $(OBJS) $(OBJS:.o=.P)
