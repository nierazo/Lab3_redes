/* bono: mismo subscriber pero sobre QUIC. manda un SUB:<tema> por
 * cada partido de interes sobre el stream por defecto de la conexion,
 * y se queda leyendo en bucle lo que el broker le vaya reenviando.
 *
 * uso: ./subscriber_quic <ip_broker> <puerto> <tema1> [tema2 ...]
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

static unsigned char buf_alpn[64];
static unsigned int len_alpn;

static void armar_alpn(void) {
    size_t plen = strlen(QUIC_ALPN_PROTO);
    buf_alpn[0] = (unsigned char)plen;
    memcpy(buf_alpn + 1, QUIC_ALPN_PROTO, plen);
    len_alpn = (unsigned int)(plen + 1);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        exit(1);
    }
    const char *ip_remota = argv[1];
    int puerto = atoi(argv[2]);

    armar_alpn();

    SSL_CTX *ctx = SSL_CTX_new(OSSL_QUIC_client_method());
    if (!ctx) { ERR_print_errors_fp(stderr); exit(1); }
    SSL_CTX_set_alpn_protos(ctx, buf_alpn, len_alpn);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    fcntl(fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in dir_remota;
    memset(&dir_remota, 0, sizeof(dir_remota));
    dir_remota.sin_family = AF_INET;
    dir_remota.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip_remota, &dir_remota.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", ip_remota);
        exit(1);
    }
    if (connect(fd, (struct sockaddr *)&dir_remota, sizeof(dir_remota)) < 0) {
        perror("connect"); exit(1);
    }

    BIO_ADDR *par = BIO_ADDR_new();
    BIO_ADDR_rawmake(par, AF_INET, &dir_remota.sin_addr, sizeof(dir_remota.sin_addr),
                      dir_remota.sin_port);

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set1_initial_peer_addr(ssl, par);
    SSL_set1_host(ssl, "localhost");
    SSL_set_blocking_mode(ssl, 1);

    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "[subscriber] fallo el handshake QUIC\n");
        ERR_print_errors_fp(stderr);
        exit(1);
    }
    printf("[subscriber] conexion QUIC establecida con %s:%d\n", ip_remota, puerto);

    for (int i = 3; i < argc; i++) {
        char linea[TAM_BUFER];
        int len = snprintf(linea, sizeof(linea), "SUB:%s\n", argv[i]);
        size_t escritos = 0;
        SSL_write_ex(ssl, linea, (size_t)len, &escritos);
        printf("[subscriber] suscrito al tema '%s'\n", argv[i]);
    }
    printf("[subscriber] esperando actualizaciones...\n");

    char buf_rx[TAM_BUFER];
    int len_rx = 0;
    while (1) {
        int espacio = TAM_BUFER - len_rx;
        if (espacio <= 0) {
            fprintf(stderr, "[subscriber] linea recibida demasiado larga\n");
            break;
        }
        size_t n = 0;
        if (SSL_read_ex(ssl, buf_rx + len_rx, espacio, &n) <= 0 || n == 0) {
            printf("[subscriber] el broker cerro la conexion\n");
            break;
        }
        len_rx += (int)n;

        // mismo truco que en subscriber_tcp.c: un stream QUIC tambien
        // es un flujo de bytes fiable y en orden, no datagramas sueltos
        char *inicio = buf_rx;
        char *nl;
        while ((nl = memchr(inicio, '\n', len_rx - (inicio - buf_rx))) != NULL) {
            *nl = '\0';
            if (strncmp(inicio, "MSG:", 4) == 0) {
                char *tema = inicio + 4;
                char *sep = strchr(tema, ':');
                if (sep != NULL) {
                    *sep = '\0';
                    printf(">> [%s] %s\n", tema, sep + 1);
                }
            }
            inicio = nl + 1;
        }
        int restante = len_rx - (inicio - buf_rx);
        memmove(buf_rx, inicio, restante);
        len_rx = restante;
    }

    SSL_free(ssl);
    BIO_ADDR_free(par);
    close(fd);
    return 0;
}
