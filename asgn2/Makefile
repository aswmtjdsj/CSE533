CC = gcc
LIBS =
OBJS = utils.o prifinfo_plus.o mainloop.o
prifinfo_plus_OBJS = utils.o prifinfo_plus.o
UNAME := $(shell uname -s)
CFLAGS = -g
ifeq ($(UNAME),SunOS)
	LIBS += -lnsl -lsocket -lresolv
	CFLAGS += -DBSD_COMP
endif


all: $(OBJS) prifinfo_plus

prifinfo_plus: $(prifinfo_plus_OBJS)
	$(CC) $(CFLAGS) -o prifinfo_plus $(prifinfo_plus_OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm prifinfo_plus $(OBJS)

