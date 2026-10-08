/* bono: mismo publisher pero sobre QUIC. abre la conexion (con su
 * handshake TLS 1.3 de por medio) y despues escribe cada evento sobre
 * el stream por defecto, igual que publisher_tcp.c escribe en su socket.
 *
 * uso: ./publisher_quic <ip_broker> <puerto> <tema> [num_mensajes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* compatibilidad Windows / Linux */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <openssl/ssl.h>
#include <openssl/quic.h>
#include <openssl/err.h>
#include <openssl/bio.h>

#include "common.h"
#include "quic_common.h"


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


static unsigned char buf_alpn[64];
static unsigned int len_alpn;


static void armar_alpn(void) {

    size_t plen = strlen(QUIC_ALPN_PROTO);

    buf_alpn[0] = (unsigned char)plen;

    memcpy(
        buf_alpn + 1,
        QUIC_ALPN_PROTO,
        plen
    );

    len_alpn = (unsigned int)(plen + 1);
}


int main(int argc, char *argv[]) {

    if (argc < 4) {

        fprintf(
            stderr,
            "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes]\n",
            argv[0]
        );

        exit(1);
    }


    const char *ip_remota = argv[1];
    int puerto = atoi(argv[2]);
    const char *tema = argv[3];

    int num_mensajes =
        (argc >= 5)
        ? atoi(argv[4])
        : 10;


    /* Windows necesita inicializar Winsock */
#ifdef _WIN32

    WSADATA wsa;

    if (
        WSAStartup(
            MAKEWORD(2, 2),
            &wsa
        ) != 0
    ) {

        fprintf(
            stderr,
            "[publisher] error inicializando Winsock\n"
        );

        return 1;
    }

#endif


    armar_alpn();


    SSL_CTX *ctx =
        SSL_CTX_new(
            OSSL_QUIC_client_method()
        );


    if (!ctx) {

        ERR_print_errors_fp(stderr);

#ifdef _WIN32
        WSACleanup();
#endif

        exit(1);
    }


    SSL_CTX_set_alpn_protos(
        ctx,
        buf_alpn,
        len_alpn
    );


    /* el cert del broker es autofirmado (es de laboratorio,
     * no hay CA real detras), asi que no tiene caso validar
     * la cadena de confianza
     */
    SSL_CTX_set_verify(
        ctx,
        SSL_VERIFY_NONE,
        NULL
    );


    /* crear socket UDP */
#ifdef _WIN32

    SOCKET fd =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );


    if (fd == INVALID_SOCKET) {

        fprintf(
            stderr,
            "[publisher] error creando socket UDP: %d\n",
            WSAGetLastError()
        );

        SSL_CTX_free(ctx);

        WSACleanup();

        return 1;
    }

#else

    int fd =
        socket(
            AF_INET,
            SOCK_DGRAM,
            0
        );


    if (fd < 0) {

        perror("socket");

        SSL_CTX_free(ctx);

        exit(1);
    }

#endif


    /* OpenSSL maneja la E/S de QUIC por su cuenta,
     * por eso el socket debe quedar no bloqueante
     */
#ifdef _WIN32

    u_long modo = 1;


    if (
        ioctlsocket(
            fd,
            FIONBIO,
            &modo
        )
        != 0
    ) {

        fprintf(
            stderr,
            "[publisher] no se pudo poner el socket en modo no bloqueante\n"
        );

        closesocket(fd);

        SSL_CTX_free(ctx);

        WSACleanup();

        return 1;
    }

#else

    fcntl(
        fd,
        F_SETFL,
        O_NONBLOCK
    );

#endif


    struct sockaddr_in dir_remota;


    memset(
        &dir_remota,
        0,
        sizeof(dir_remota)
    );


    dir_remota.sin_family =
        AF_INET;

    dir_remota.sin_port =
        htons(puerto);


    if (
        inet_pton(
            AF_INET,
            ip_remota,
            &dir_remota.sin_addr
        )
        <= 0
    ) {

        fprintf(
            stderr,
            "Direccion IP invalida: %s\n",
            ip_remota
        );

#ifdef _WIN32
        closesocket(fd);
        WSACleanup();
#else
        close(fd);
#endif

        SSL_CTX_free(ctx);

        return 1;
    }


    /* conectar el socket UDP con el broker */
#ifdef _WIN32

    if (
        connect(
            fd,
            (struct sockaddr *)&dir_remota,
            sizeof(dir_remota)
        )
        == SOCKET_ERROR
    ) {

        fprintf(
            stderr,
            "[publisher] connect fallo: %d\n",
            WSAGetLastError()
        );

        closesocket(fd);

        SSL_CTX_free(ctx);

        WSACleanup();

        return 1;
    }

#else

    if (
        connect(
            fd,
            (struct sockaddr *)&dir_remota,
            sizeof(dir_remota)
        )
        < 0
    ) {

        perror("connect");

        close(fd);

        SSL_CTX_free(ctx);

        return 1;
    }

#endif


    BIO_ADDR *par =
        BIO_ADDR_new();


    if (!par) {

        fprintf(
            stderr,
            "[publisher] no se pudo crear BIO_ADDR\n"
        );

#ifdef _WIN32
        closesocket(fd);
        WSACleanup();
#else
        close(fd);
#endif

        SSL_CTX_free(ctx);

        return 1;
    }


    BIO_ADDR_rawmake(
        par,
        AF_INET,
        &dir_remota.sin_addr,
        sizeof(dir_remota.sin_addr),
        dir_remota.sin_port
    );


    SSL *ssl =
        SSL_new(ctx);


    if (!ssl) {

        ERR_print_errors_fp(stderr);

        BIO_ADDR_free(par);

#ifdef _WIN32
        closesocket(fd);
        WSACleanup();
#else
        close(fd);
#endif

        SSL_CTX_free(ctx);

        return 1;
    }


    SSL_set_fd(
        ssl,
        (int)fd
    );


    SSL_set1_initial_peer_addr(
        ssl,
        par
    );


    SSL_set1_host(
        ssl,
        "localhost"
    );


    SSL_set_blocking_mode(
        ssl,
        1
    );


    if (
        SSL_connect(ssl) != 1
    ) {

        fprintf(
            stderr,
            "[publisher] fallo el handshake QUIC\n"
        );

        ERR_print_errors_fp(stderr);

        SSL_free(ssl);

        BIO_ADDR_free(par);

#ifdef _WIN32
        closesocket(fd);
        WSACleanup();
#else
        close(fd);
#endif

        SSL_CTX_free(ctx);

        return 1;
    }


    printf(
        "[publisher] conexion QUIC establecida con %s:%d, publicando en tema '%s'\n",
        ip_remota,
        puerto,
        tema
    );


    int minuto = 1;


    for (
        int i = 0;
        i < num_mensajes;
        i++
    ) {

        char texto[TEXTO_MAX];

        int idx =
            i % N_EVENTOS;


        if (
            strchr(
                eventos[idx],
                '%'
            )
            != NULL
        ) {

            snprintf(
                texto,
                sizeof(texto),
                eventos[idx],
                minuto
            );

        } else {

            snprintf(
                texto,
                sizeof(texto),
                "%s",
                eventos[idx]
            );
        }


        char linea[TAM_BUFER];


        int len =
            snprintf(
                linea,
                sizeof(linea),
                "MSG:%s:%s\n",
                tema,
                texto
            );


        size_t escritos = 0;


        if (
            SSL_write_ex(
                ssl,
                linea,
                (size_t)len,
                &escritos
            )
            <= 0
        ) {

            fprintf(
                stderr,
                "[publisher] fallo el envio\n"
            );

            ERR_print_errors_fp(stderr);

            break;
        }


        printf(
            "[publisher] enviado: %s",
            linea
        );


        minuto +=
            2 + (i % 3);


        /* esperar un segundo entre mensajes */
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }


    SSL_shutdown(ssl);

    SSL_free(ssl);

    BIO_ADDR_free(par);

    SSL_CTX_free(ctx);


#ifdef _WIN32

    closesocket(fd);

    WSACleanup();

#else

    close(fd);

#endif


    return 0;
}