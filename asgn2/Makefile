CC = gcc
LIBS =
OBJS = utils.o prifinfo_plus.o
UNAME := $(shell uname -s)
ifeq ($(UNAME),SunOS)
	LIBS += -lnsl -lsocket -lresolv
endif

CFLAGS = -g

all: $(OBJS) prifinfo_plus

prifinfo_plus: $(OBJS)
	$(CC) $(CFLAGS) -o prifinfo_plus $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm prifinfo_plus $(OBJS)

