CC = gcc
CFLAGS = -Wall -Wextra -std=c99

TARGETS = broker_tcp publisher_tcp subscriber_tcp broker_udp publisher_udp subscriber_udp

all: $(TARGETS)

broker_tcp: broker_tcp.c common.h
	$(CC) $(CFLAGS) -o $@ broker_tcp.c

publisher_tcp: publisher_tcp.c common.h
	$(CC) $(CFLAGS) -o $@ publisher_tcp.c

subscriber_tcp: subscriber_tcp.c common.h
	$(CC) $(CFLAGS) -o $@ subscriber_tcp.c

broker_udp: broker_udp.c common.h
	$(CC) $(CFLAGS) -o $@ broker_udp.c

publisher_udp: publisher_udp.c common.h
	$(CC) $(CFLAGS) -o $@ publisher_udp.c

subscriber_udp: subscriber_udp.c common.h
	$(CC) $(CFLAGS) -o $@ subscriber_udp.c

clean:
	rm -f $(TARGETS)

.PHONY: all clean
