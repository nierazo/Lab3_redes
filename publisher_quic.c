/*
 * publisher_quic.c
 * BONO: publicador (periodista deportivo) que envia eventos de un
 * partido al broker usando QUIC. Abre una conexion QUIC contra el
 * broker (con su handshake TLS 1.3 incluido) y luego escribe cada
 * evento sobre el stream bidireccional por defecto de esa conexion.
 *
 * Uso:
 * publisher_quic.exe <ip_broker> <puerto> <tema> [num_mensajes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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


/* Eventos deportivos de prueba */
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


/* Construye el identificador ALPN */
static void build_alpn_wire(void)
{
    size_t plen = strlen(QUIC_ALPN_PROTO);

    alpn_wire[0] = (unsigned char)plen;

    memcpy(
        alpn_wire + 1,
        QUIC_ALPN_PROTO,
        plen
    );

    alpn_wire_len = (unsigned int)(plen + 1);
}


int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(
            stderr,
            "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes]\n",
            argv[0]
        );

        return 1;
    }


    const char *server_ip = argv[1];

    int port = atoi(argv[2]);

    const char *topic = argv[3];

    int num_messages =
        (argc >= 5)
        ? atoi(argv[4])
        : 10;


    /* =========================
       INICIALIZAR WINSOCK
       ========================= */

#ifdef _WIN32

    WSADATA wsa;

    if (
        WSAStartup(
            MAKEWORD(2, 2),
            &wsa
        ) != 0
    )
    {
        fprintf(
            stderr,
            "[publisher] error inicializando Winsock\n"
        );

        return 1;
    }

#endif


    build_alpn_wire();


    /* =========================
       CONTEXTO QUIC
       ========================= */

    SSL_CTX *ctx =
        SSL_CTX_new(
            OSSL_QUIC_client_method()
        );


    if (!ctx)
    {
        ERR_print_errors_fp(stderr);

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }


    SSL_CTX_set_alpn_protos(
        ctx,
        alpn_wire,
        alpn_wire_len
    );


    /*
     * Para el laboratorio usamos certificado autofirmado,
     * por eso no se valida contra una CA real.
     */
    SSL_CTX_set_verify(
        ctx,
        SSL_VERIFY_NONE,
        NULL
    );


    /* =========================
       CREAR SOCKET UDP
       ========================= */

#ifdef _WIN32

    SOCKET fd =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );


    if (fd == INVALID_SOCKET)
    {
        fprintf(
            stderr,
            "[publisher] error creando socket: %d\n",
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


    if (fd < 0)
    {
        perror("socket");

        SSL_CTX_free(ctx);

        return 1;
    }

#endif


    /* =========================
       SOCKET NO BLOQUEANTE
       ========================= */

#ifdef _WIN32

    u_long mode = 1;

    if (
        ioctlsocket(
            fd,
            FIONBIO,
            &mode
        )
        != 0
    )
    {
        fprintf(
            stderr,
            "[publisher] error configurando socket no bloqueante\n"
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


    /* =========================
       DIRECCION DEL BROKER
       ========================= */

    struct sockaddr_in server_addr;

    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(port);


    if (
        inet_pton(
            AF_INET,
            server_ip,
            &server_addr.sin_addr
        )
        <= 0
    )
    {
        fprintf(
            stderr,
            "Direccion IP invalida: %s\n",
            server_ip
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


    /* =========================
       CONECTAR SOCKET UDP
       ========================= */

#ifdef _WIN32

    if (
        connect(
            fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        )
        == SOCKET_ERROR
    )
    {
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
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        )
        < 0
    )
    {
        perror("connect");

        close(fd);
        SSL_CTX_free(ctx);

        return 1;
    }

#endif


    /* =========================
       DIRECCION QUIC
       ========================= */

    BIO_ADDR *peer =
        BIO_ADDR_new();


    if (!peer)
    {
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


    if (
        !BIO_ADDR_rawmake(
            peer,
            AF_INET,
            &server_addr.sin_addr,
            sizeof(server_addr.sin_addr),
            server_addr.sin_port
        )
    )
    {
        fprintf(
            stderr,
            "[publisher] no se pudo configurar BIO_ADDR\n"
        );

        BIO_ADDR_free(peer);

#ifdef _WIN32
        closesocket(fd);
        WSACleanup();
#else
        close(fd);
#endif

        SSL_CTX_free(ctx);

        return 1;
    }


    /* =========================
       CREAR CONEXION QUIC
       ========================= */

    SSL *ssl =
        SSL_new(ctx);


    if (!ssl)
    {
        ERR_print_errors_fp(stderr);

        BIO_ADDR_free(peer);

#ifdef _WIN32
        closesocket(fd);
        WSACleanup();
#else
        close(fd);
#endif

        SSL_CTX_free(ctx);

        return 1;
    }


    if (
        !SSL_set_fd(
            ssl,
            (int)fd
        )
    )
    {
        ERR_print_errors_fp(stderr);

        return 1;
    }


    if (
        !SSL_set1_initial_peer_addr(
            ssl,
            peer
        )
    )
    {
        ERR_print_errors_fp(stderr);

        return 1;
    }


    SSL_set1_host(
        ssl,
        "localhost"
    );


    SSL_set_blocking_mode(
        ssl,
        1
    );


    /* =========================
       HANDSHAKE QUIC
       ========================= */

    if (
        SSL_connect(ssl) != 1
    )
    {
        fprintf(
            stderr,
            "[publisher] fallo el handshake QUIC\n"
        );

        ERR_print_errors_fp(stderr);

        SSL_free(ssl);
        BIO_ADDR_free(peer);

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
        server_ip,
        port,
        topic
    );


    /* =========================
       ENVIAR MENSAJES
       ========================= */

    int minute = 1;


    for (
        int i = 0;
        i < num_messages;
        i++
    )
    {
        char text[TEXT_MAX];


        int idx =
            i % N_EVENTS;


        /*
         * Algunos eventos contienen %d,
         * por ejemplo los goles.
         */
        if (
            strchr(
                events[idx],
                '%'
            )
            != NULL
        )
        {
            snprintf(
                text,
                sizeof(text),
                events[idx],
                minute
            );
        }

        else
        {
            snprintf(
                text,
                sizeof(text),
                "%s",
                events[idx]
            );
        }


        char line[BUFFER_SIZE];


        int len =
            snprintf(
                line,
                sizeof(line),
                "MSG:%s:%s\n",
                topic,
                text
            );


        size_t written = 0;


        if (
            !SSL_write_ex(
                ssl,
                line,
                (size_t)len,
                &written
            )
        )
        {
            fprintf(
                stderr,
                "[publisher] fallo el envio\n"
            );

            ERR_print_errors_fp(stderr);

            break;
        }


        printf(
            "[publisher] enviado: %s",
            line
        );


        minute +=
            2 + (i % 3);


        /* Esperar 1 segundo entre mensajes */

#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }


    /* =========================
       LIMPIEZA
       ========================= */

    SSL_shutdown(ssl);

    SSL_free(ssl);

    BIO_ADDR_free(peer);

    SSL_CTX_free(ctx);


#ifdef _WIN32

    closesocket(fd);

    WSACleanup();

#else

    close(fd);

#endif


    return 0;
}