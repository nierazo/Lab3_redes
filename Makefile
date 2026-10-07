CC = gcc
CFLAGS = -Wall -Wextra -std=c99

TARGETS = broker_tcp publisher_tcp subscriber_tcp broker_udp publisher_udp subscriber_udp

# bono: version QUIC (usa el QUIC que trae OpenSSL >= 3.5).
# queda fuera de "make all" a proposito, para que el entregable base
# compile aunque no haya OpenSSL con soporte de QUIC instalado.
QUIC_TARGETS = broker_quic publisher_quic subscriber_quic

QUIC_PREFIX := $(shell pkg-config --exists openssl 2>/dev/null && pkg-config --variable=prefix openssl)
ifeq ($(QUIC_PREFIX),)
QUIC_PREFIX := $(shell brew --prefix openssl@3 2>/dev/null)
endif
ifneq ($(QUIC_PREFIX),)
QUIC_CFLAGS = -I$(QUIC_PREFIX)/include
QUIC_LDFLAGS = -L$(QUIC_PREFIX)/lib
endif
QUIC_LIBS = -lssl -lcrypto -lpthread

all: $(TARGETS)

quic: $(QUIC_TARGETS)

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

broker_quic: broker_quic.c common.h quic_common.h
	$(CC) $(CFLAGS) $(QUIC_CFLAGS) -o $@ broker_quic.c $(QUIC_LDFLAGS) $(QUIC_LIBS)

publisher_quic: publisher_quic.c common.h quic_common.h
	$(CC) $(CFLAGS) $(QUIC_CFLAGS) -o $@ publisher_quic.c $(QUIC_LDFLAGS) $(QUIC_LIBS)

subscriber_quic: subscriber_quic.c common.h quic_common.h
	$(CC) $(CFLAGS) $(QUIC_CFLAGS) -o $@ subscriber_quic.c $(QUIC_LDFLAGS) $(QUIC_LIBS)

# genera un certificado autofirmado para que el broker QUIC pueda hacer
# el handshake TLS 1.3 que exige el protocolo
certs:
	openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 \
		-keyout key.pem -out cert.pem -days 365 -nodes -subj "/CN=localhost"

clean:
	rm -f $(TARGETS) $(QUIC_TARGETS)

.PHONY: all quic clean certs
