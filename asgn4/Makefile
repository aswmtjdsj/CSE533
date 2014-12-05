CC = gcc
LIBS = -lrt
tour_OBJS = tour.o utils.o api.o mainloop.o
arp_OBJS = arp.o utils.o api.o
OBJS = $(tour_OBJS) $(arp_OBJS)
ID = syuxuan
BINARIES = $(ID)_tour $(ID)_arp
CFLAGS = -g -Wall -Wno-unused-function -DID=\"$(ID)\"

all: $(OBJS) $(BINARIES)

%.P : %.c
	gcc -MM -MT $(<:.c=.o) -MT $(<:.c=.P) -MF $@ $<

-include $(OBJS:.o=.P)

$(ID)_tour: $(tour_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(ID)_arp: $(arp_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm $(BINARIES) $(OBJS) $(OBJS:.o=.P)
