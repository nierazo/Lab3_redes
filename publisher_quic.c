/*
 * Publicador de eventos deportivos usando QUIC.
 *
 * Abre una conexion QUIC contra el broker (con su handshake TLS 1.3
 * incluido) y despues escribe cada evento sobre el stream por
 * defecto de esa conexion, igual que publisher_tcp.c escribe sobre
 * su socket.
 *
 * Uso:
 *   ./publisher_quic <ip_broker> <puerto> <tema> [num_mensajes]
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
 * Plantillas de eventos que va narrando el publicador, ciclando por
 * el arreglo con el indice del mensaje (i % N_EVENTOS). Los goles
 * llevan un "%d" porque ademas necesitan el minuto del partido; el
 * resto de eventos no lo necesita y se usan tal cual.
 */
static const char *eventos[] = {
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
#define N_EVENTOS (int)(sizeof(eventos) / sizeof(eventos[0]))


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
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes]\n", argv[0]);
        return 1;
    }

    const char *ip_remota = argv[1];
    int puerto = atoi(argv[2]);
    const char *tema = argv[3];
    int num_mensajes = (argc >= 5) ? atoi(argv[4]) : 10;

    configurar_alpn();

    SSL_CTX *contexto = SSL_CTX_new(OSSL_QUIC_client_method());

    if (contexto == NULL) {
        ERR_print_errors_fp(stderr);
        return 1;
    }

    SSL_CTX_set_alpn_protos(contexto, protocolos_alpn, longitud_alpn);

    /*
     * El certificado del broker es autofirmado (es de laboratorio,
     * no hay una CA real detras), asi que no tiene sentido validar
     * la cadena de confianza como se haria con un certificado real.
     */
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
        fprintf(stderr, "[publisher] fallo el handshake QUIC\n");
        ERR_print_errors_fp(stderr);
        BIO_ADDR_free(par);
        SSL_free(ssl);
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    printf(
        "[publisher] conexion QUIC establecida con %s:%d, publicando en tema '%s'\n",
        ip_remota,
        puerto,
        tema
    );

    int minuto = 1;

    /*
     * Se manda un mensaje por vuelta, dejando un sleep(1) entre
     * cada uno para que el partido se vea narrado en tiempo real
     * y para que la captura de Wireshark muestre los paquetes
     * separados en el tiempo en vez de una rafaga instantanea.
     *
     * No se llamo a SSL_new_stream en ningun lado: al escribir
     * directamente sobre la conexion (ssl), QUIC usa el stream
     * bidireccional por defecto, que para este programa cumple el
     * mismo papel que la conexion TCP en publisher_tcp.c.
     */
    for (int i = 0; i < num_mensajes; i++) {
        char texto[TEXTO_MAX];
        int idx = i % N_EVENTOS;

        if (strchr(eventos[idx], '%') != NULL) {
            snprintf(texto, sizeof(texto), eventos[idx], minuto);
        } else {
            snprintf(texto, sizeof(texto), "%s", eventos[idx]);
        }

        char mensaje[TAM_BUFER];
        int longitud = snprintf(mensaje, sizeof(mensaje), "MSG:%s:%s\n", tema, texto);

        size_t bytes_escritos = 0;

        if (SSL_write_ex(ssl, mensaje, (size_t)longitud, &bytes_escritos) != 1) {
            fprintf(stderr, "[publisher] fallo el envio\n");
            ERR_print_errors_fp(stderr);
            break;
        }

        printf("[publisher] enviado: %s", mensaje);

        minuto += 2 + (i % 3);
        sleep(1);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    BIO_ADDR_free(par);
    close(socket_udp);
    SSL_CTX_free(contexto);

    return 0;
}
