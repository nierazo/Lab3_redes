/*
 * subscriber_quic.c
 * BONO: suscriptor (hincha) que se conecta al broker por QUIC, envia
 * un SUB:<tema> por cada partido de interes sobre el stream por
 * defecto de la conexion, y despues queda leyendo en bucle las
 * actualizaciones que el broker le reenvia.
 *
 * Uso: ./subscriber_quic <ip_broker> <puerto> <tema1> [tema2 ...]
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

static unsigned char alpn_wire[64];
static unsigned int alpn_wire_len;

static void build_alpn_wire(void) {
    size_t plen = strlen(QUIC_ALPN_PROTO);
    alpn_wire[0] = (unsigned char)plen;
    memcpy(alpn_wire + 1, QUIC_ALPN_PROTO, plen);
    alpn_wire_len = (unsigned int)(plen + 1);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        exit(1);
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    build_alpn_wire();

    SSL_CTX *ctx = SSL_CTX_new(OSSL_QUIC_client_method());
    if (!ctx) { ERR_print_errors_fp(stderr); exit(1); }
    SSL_CTX_set_alpn_protos(ctx, alpn_wire, alpn_wire_len);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    fcntl(fd, F_SETFL, O_NONBLOCK);

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
        fprintf(stderr, "[subscriber] fallo el handshake QUIC\n");
        ERR_print_errors_fp(stderr);
        exit(1);
    }
    printf("[subscriber] conexion QUIC establecida con %s:%d\n", server_ip, port);

    for (int i = 3; i < argc; i++) {
        char line[BUFFER_SIZE];
        int len = snprintf(line, sizeof(line), "SUB:%s\n", argv[i]);
        size_t written = 0;
        SSL_write_ex(ssl, line, (size_t)len, &written);
        printf("[subscriber] suscrito al tema '%s'\n", argv[i]);
    }
    printf("[subscriber] esperando actualizaciones...\n");

    char inbuf[BUFFER_SIZE];
    int inlen = 0;
    while (1) {
        int space = BUFFER_SIZE - inlen;
        if (space <= 0) {
            fprintf(stderr, "[subscriber] linea recibida demasiado larga\n");
            break;
        }
        size_t n = 0;
        if (SSL_read_ex(ssl, inbuf + inlen, space, &n) <= 0 || n == 0) {
            printf("[subscriber] el broker cerro la conexion\n");
            break;
        }
        inlen += (int)n;

        /* un stream QUIC tambien es un flujo de bytes fiable y
         * ordenado (como un socket TCP), por eso se ensamblan lineas
         * de la misma forma que en subscriber_tcp.c */
        char *start = inbuf;
        char *nl;
        while ((nl = memchr(start, '\n', inlen - (start - inbuf))) != NULL) {
            *nl = '\0';
            if (strncmp(start, "MSG:", 4) == 0) {
                char *topic = start + 4;
                char *sep = strchr(topic, ':');
                if (sep != NULL) {
                    *sep = '\0';
                    printf(">> [%s] %s\n", topic, sep + 1);
                }
            }
            start = nl + 1;
        }
        int remaining = inlen - (start - inbuf);
        memmove(inbuf, start, remaining);
        inlen = remaining;
    }

    SSL_free(ssl);
    BIO_ADDR_free(peer);
    close(fd);
    return 0;
}
