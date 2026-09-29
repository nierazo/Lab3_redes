/*
 * publisher_quic.c
 * BONO: publicador (periodista deportivo) que envia eventos de un
 * partido al broker usando QUIC. Abre una conexion QUIC contra el
 * broker (con su handshake TLS 1.3 incluido) y luego escribe cada
 * evento sobre el stream bidireccional por defecto de esa conexion,
 * exactamente igual que publisher_tcp.c escribe sobre su socket.
 *
 * Uso: ./publisher_quic <ip_broker> <puerto> <tema> [num_mensajes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/quic.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include "common.h"
#include "quic_common.h"

static const char *events[] = {
    "Gol de Equipo A al minuto %d",
    "Gol de Equipo B al minuto %d",
    "Cambio: jugador 10 entra por jugador 20",
    "Tarjeta amarilla al numero 7 de Equipo A",
    "Tarjeta amarilla al numero 4 de Equipo B",
    "Tarjeta roja al numero 9 de Equipo B",
    "Tiro de esquina para Equipo A",
    "Fuera de lugar senalado contra Equipo B",
    "Doble cambio en Equipo A",
    "Final del primer tiempo"
};
#define N_EVENTS (int)(sizeof(events) / sizeof(events[0]))

static unsigned char alpn_wire[64];
static unsigned int alpn_wire_len;

static void build_alpn_wire(void) {
    size_t plen = strlen(QUIC_ALPN_PROTO);
    alpn_wire[0] = (unsigned char)plen;
    memcpy(alpn_wire + 1, QUIC_ALPN_PROTO, plen);
    alpn_wire_len = (unsigned int)(plen + 1);
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes]\n", argv[0]);
        exit(1);
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];
    int num_messages = (argc >= 5) ? atoi(argv[4]) : 10;

    build_alpn_wire();

    SSL_CTX *ctx = SSL_CTX_new(OSSL_QUIC_client_method());
    if (!ctx) { ERR_print_errors_fp(stderr); exit(1); }
    SSL_CTX_set_alpn_protos(ctx, alpn_wire, alpn_wire_len);
    /* el broker usa un certificado autofirmado hecho para este
     * laboratorio, asi que no hay una CA real contra la cual validarlo */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    fcntl(fd, F_SETFL, O_NONBLOCK); /* QUIC de OpenSSL administra la E/S internamente */

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", server_ip);
        exit(1);
    }
    if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect"); exit(1);
    }

    BIO_ADDR *peer = BIO_ADDR_new();
    BIO_ADDR_rawmake(peer, AF_INET, &server_addr.sin_addr, sizeof(server_addr.sin_addr),
                      server_addr.sin_port);

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set1_initial_peer_addr(ssl, peer);
    SSL_set1_host(ssl, "localhost");
    SSL_set_blocking_mode(ssl, 1);

    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "[publisher] fallo el handshake QUIC\n");
        ERR_print_errors_fp(stderr);
        exit(1);
    }
    printf("[publisher] conexion QUIC establecida con %s:%d, publicando en tema '%s'\n",
           server_ip, port, topic);

    int minute = 1;
    for (int i = 0; i < num_messages; i++) {
        char text[TEXT_MAX];
        int idx = i % N_EVENTS;
        if (strchr(events[idx], '%') != NULL)
            snprintf(text, sizeof(text), events[idx], minute);
        else
            snprintf(text, sizeof(text), "%s", events[idx]);

        char line[BUFFER_SIZE];
        int len = snprintf(line, sizeof(line), "MSG:%s:%s\n", topic, text);

        size_t written = 0;
        if (SSL_write_ex(ssl, line, (size_t)len, &written) <= 0) {
            fprintf(stderr, "[publisher] fallo el envio\n");
            ERR_print_errors_fp(stderr);
            break;
        }
        printf("[publisher] enviado: %s", line);

        minute += 2 + (i % 3);
        sleep(1);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    BIO_ADDR_free(peer);
    close(fd);
    return 0;
}
