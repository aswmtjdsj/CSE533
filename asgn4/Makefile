CC = clang
LIBS = -lrt
tour_OBJS = tour.o utils.o mainloop.o
arp_OBJS = arp.o utils.o mainloop.o
arp_test_OBJS = arp_test.o arp_client.o
OBJS = $(tour_OBJS) $(arp_OBJS)
ID = syuxuan
BINARIES = $(ID)_tour $(ID)_arp $(ID)_arp_test
CFLAGS = -g -Wall -Wno-unused-function

all: $(OBJS) $(BINARIES)

%.P : %.c
	gcc -MM -MT $(<:.c=.o) -MT $(<:.c=.P) -MF $@ $<

-include $(OBJS:.o=.P)

$(ID)_tour: $(tour_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(ID)_arp: $(arp_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(ID)_arp_test: $(arp_test_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm $(BINARIES) $(OBJS) $(OBJS:.o=.P)
