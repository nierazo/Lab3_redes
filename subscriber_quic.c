/*
 * Suscriptor de eventos deportivos usando QUIC.
 *
 * Abre una conexion QUIC contra el broker, manda un SUB:tema por
 * cada partido de interes sobre el stream por defecto de esa
 * conexion, y se queda leyendo en bucle lo que el broker le vaya
 * reenviando.
 *
 * Uso:
 *   ./subscriber_quic <ip_broker> <puerto> <tema1> [tema2 ...]
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


/*
 * ALPN permite que cliente y servidor acuerden el protocolo de
 * aplicacion que van a utilizar durante el handshake TLS. OpenSSL
 * espera cada protocolo precedido por un byte que indique su
 * longitud, no como una cadena C convencional.
 */
static unsigned char protocolos_alpn[64];
static unsigned int longitud_alpn;


static void configurar_alpn(void) {
    size_t longitud = strlen(QUIC_ALPN_PROTO);

    protocolos_alpn[0] = (unsigned char)longitud;
    memcpy(protocolos_alpn + 1, QUIC_ALPN_PROTO, longitud);

    longitud_alpn = (unsigned int)(longitud + 1);
}


int main(int argc, char *argv[]) {
    /*
     * Hace que los mensajes de la consola se muestren por linea,
     * algo util para ver las actualizaciones a medida que llegan.
     */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        return 1;
    }

    const char *ip_remota = argv[1];
    int puerto = atoi(argv[2]);

    configurar_alpn();

    SSL_CTX *contexto = SSL_CTX_new(OSSL_QUIC_client_method());

    if (contexto == NULL) {
        ERR_print_errors_fp(stderr);
        return 1;
    }

    SSL_CTX_set_alpn_protos(contexto, protocolos_alpn, longitud_alpn);
    SSL_CTX_set_verify(contexto, SSL_VERIFY_NONE, NULL);

    int socket_udp = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_udp < 0) {
        perror("socket");
        SSL_CTX_free(contexto);
        return 1;
    }

    /*
     * OpenSSL administra la E/S de QUIC por su cuenta, para eso
     * necesita que el descriptor quede en modo no bloqueante.
     */
    int flags = fcntl(socket_udp, F_GETFL, 0);

    if (flags < 0 || fcntl(socket_udp, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl");
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    struct sockaddr_in direccion_remota;
    memset(&direccion_remota, 0, sizeof(direccion_remota));

    direccion_remota.sin_family = AF_INET;
    direccion_remota.sin_port = htons((unsigned short)puerto);

    if (inet_pton(AF_INET, ip_remota, &direccion_remota.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", ip_remota);
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    if (connect(
            socket_udp,
            (struct sockaddr *)&direccion_remota,
            sizeof(direccion_remota)
        ) < 0) {

        perror("connect");
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    /*
     * Aunque el socket UDP ya esta "conectado" (fijamos su destino
     * con connect()), OpenSSL maneja sus propios datagramas con
     * sendto()/recvfrom() internamente y necesita que le digamos la
     * direccion del broker por su cuenta: por eso se arma este
     * BIO_ADDR y se pasa a SSL_set1_initial_peer_addr.
     */
    BIO_ADDR *par = BIO_ADDR_new();

    BIO_ADDR_rawmake(
        par,
        AF_INET,
        &direccion_remota.sin_addr,
        sizeof(direccion_remota.sin_addr),
        direccion_remota.sin_port
    );

    SSL *ssl = SSL_new(contexto);

    SSL_set_fd(ssl, socket_udp);
    SSL_set1_initial_peer_addr(ssl, par);

    /*
     * SSL_set1_host fija el nombre que se usa para SNI y para
     * verificar el certificado contra ese nombre. Como deshabilitamos
     * la verificacion arriba, aqui solo importa para SNI.
     *
     * SSL_set_blocking_mode hace que las llamadas de QUIC (connect,
     * read, write) se comporten como sockets bloqueantes de toda la
     * vida: adentro, OpenSSL se encarga de mandar/recibir paquetes,
     * reintentar y esperar ACKs; el programa no tiene que manejar
     * ese ciclo de eventos a mano.
     */
    SSL_set1_host(ssl, "localhost");
    SSL_set_blocking_mode(ssl, 1);

    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "[subscriber] fallo el handshake QUIC\n");
        ERR_print_errors_fp(stderr);
        BIO_ADDR_free(par);
        SSL_free(ssl);
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    printf("[subscriber] conexion QUIC establecida con %s:%d\n", ip_remota, puerto);

    /*
     * No se llamo a SSL_new_stream en ningun lado: al escribir
     * directamente sobre la conexion (ssl), QUIC usa el stream
     * bidireccional por defecto, el mismo que se usa mas abajo para
     * leer las actualizaciones. Los temas se reciben como argumentos
     * a partir de argv[3], y se manda un SUB por cada uno sobre ese
     * mismo stream.
     */
    for (int i = 3; i < argc; i++) {
        char mensaje[TAM_BUFER];
        int longitud = snprintf(mensaje, sizeof(mensaje), "SUB:%s\n", argv[i]);

        size_t bytes_escritos = 0;
        SSL_write_ex(ssl, mensaje, (size_t)longitud, &bytes_escritos);

        printf("[subscriber] suscrito al tema '%s'\n", argv[i]);
    }

    printf("[subscriber] esperando actualizaciones...\n");

    char buffer[TAM_BUFER];
    int bytes_buffer = 0;

    while (1) {
        int espacio = TAM_BUFER - bytes_buffer;

        if (espacio <= 0) {
            fprintf(stderr, "[subscriber] linea recibida demasiado larga\n");
            break;
        }

        size_t bytes_leidos = 0;

        if (SSL_read_ex(ssl, buffer + bytes_buffer, (size_t)espacio, &bytes_leidos) != 1
            || bytes_leidos == 0) {
            printf("[subscriber] el broker cerro la conexion\n");
            break;
        }

        bytes_buffer += (int)bytes_leidos;

        /*
         * Un stream QUIC tambien es un flujo de bytes fiable y en
         * orden (como un socket TCP), por eso se ensamblan lineas
         * de la misma forma que en subscriber_tcp.c.
         */
        char *inicio = buffer;
        char *salto_linea;

        while ((salto_linea = memchr(
                    inicio,
                    '\n',
                    (size_t)(buffer + bytes_buffer - inicio)
                )) != NULL) {

            *salto_linea = '\0';

            if (strncmp(inicio, "MSG:", 4) == 0) {
                char *tema = inicio + 4;
                char *separador = strchr(tema, ':');

                if (separador != NULL) {
                    *separador = '\0';
                    printf(">> [%s] %s\n", tema, separador + 1);
                }
            }

            inicio = salto_linea + 1;
        }

        int bytes_restantes = (int)(buffer + bytes_buffer - inicio);

        memmove(buffer, inicio, (size_t)bytes_restantes);
        bytes_buffer = bytes_restantes;
    }

    SSL_free(ssl);
    BIO_ADDR_free(par);
    close(socket_udp);
    SSL_CTX_free(contexto);

    return 0;
}
