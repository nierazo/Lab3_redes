/*
 * subscriber_quic.c
 * BONO: suscriptor que se conecta al broker mediante QUIC.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
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


static unsigned char alpn_wire[64];
static unsigned int alpn_wire_len;


/* Construye el identificador ALPN usado por QUIC */
static void build_alpn_wire(void)
{
    size_t plen = strlen(QUIC_ALPN_PROTO);

    alpn_wire[0] = (unsigned char)plen;
    memcpy(alpn_wire + 1, QUIC_ALPN_PROTO, plen);

    alpn_wire_len = (unsigned int)(plen + 1);
}


int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 4)
    {
        fprintf(
            stderr,
            "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n",
            argv[0]
        );

        return 1;
    }


    const char *server_ip = argv[1];
    int port = atoi(argv[2]);


#ifdef _WIN32

    /* Inicializar Winsock */
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        fprintf(
            stderr,
            "[subscriber] error inicializando Winsock\n"
        );

        return 1;
    }

#endif


    build_alpn_wire();


    /* Crear contexto QUIC del cliente */
    SSL_CTX *ctx =
        SSL_CTX_new(
            OSSL_QUIC_client_method()
        );

    if (!ctx)
    {
        ERR_print_errors_fp(stderr);
        return 1;
    }


    /* Configurar ALPN */
    SSL_CTX_set_alpn_protos(
        ctx,
        alpn_wire,
        alpn_wire_len
    );


    /*
     * Para este laboratorio usamos certificado autofirmado,
     * así que no verificamos la CA del broker.
     */
    SSL_CTX_set_verify(
        ctx,
        SSL_VERIFY_NONE,
        NULL
    );


    /* ========================
       CREAR SOCKET UDP
       ======================== */

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
            "[subscriber] error creando socket: %d\n",
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
        return 1;
    }

#endif


    /* QUIC necesita socket no bloqueante */

#ifdef _WIN32

    u_long mode = 1;

    if (
        ioctlsocket(
            fd,
            FIONBIO,
            &mode
        ) != 0
    )
    {
        fprintf(
            stderr,
            "[subscriber] error configurando socket no bloqueante\n"
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


    /* ========================
       DIRECCIÓN DEL BROKER
       ======================== */

    struct sockaddr_in server_addr;

    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);


    if (
        inet_pton(
            AF_INET,
            server_ip,
            &server_addr.sin_addr
        ) <= 0
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


    /* ========================
       CONNECT UDP
       ======================== */

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
            "[subscriber] connect fallo: %d\n",
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
        return 1;
    }

#endif


    /* ========================
       DIRECCIÓN PARA QUIC
       ======================== */

    BIO_ADDR *peer =
        BIO_ADDR_new();

    if (!peer)
    {
        fprintf(
            stderr,
            "[subscriber] no se pudo crear BIO_ADDR\n"
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


    /*
     * BIO_ADDR_rawmake espera dirección y puerto
     * en orden de bytes de red.
     */
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
            "[subscriber] no se pudo crear direccion QUIC\n"
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


    /* ========================
       CREAR CONEXIÓN QUIC
       ======================== */

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


    /* ========================
       HANDSHAKE QUIC
       ======================== */

    if (
        SSL_connect(ssl) != 1
    )
    {
        fprintf(
            stderr,
            "[subscriber] fallo el handshake QUIC\n"
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
        "[subscriber] conexion QUIC establecida con %s:%d\n",
        server_ip,
        port
    );


    /* ========================
       SUSCRIBIRSE A TEMAS
       ======================== */

    for (int i = 3; i < argc; i++)
    {
        char line[BUFFER_SIZE];

        int len =
            snprintf(
                line,
                sizeof(line),
                "SUB:%s\n",
                argv[i]
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
                "[subscriber] error enviando suscripcion '%s'\n",
                argv[i]
            );

            continue;
        }


        printf(
            "[subscriber] suscrito al tema '%s'\n",
            argv[i]
        );
    }


    printf(
        "[subscriber] esperando actualizaciones...\n"
    );


    /* ========================
       RECIBIR MENSAJES
       ======================== */

    char inbuf[BUFFER_SIZE];

    int inlen = 0;


    while (1)
    {
        int space =
            BUFFER_SIZE - inlen;


        if (space <= 0)
        {
            fprintf(
                stderr,
                "[subscriber] linea recibida demasiado larga\n"
            );

            break;
        }


        size_t n = 0;


        if (
            SSL_read_ex(
                ssl,
                inbuf + inlen,
                space,
                &n
            ) <= 0
            ||
            n == 0
        )
        {
            printf(
                "[subscriber] el broker cerro la conexion\n"
            );

            break;
        }


        inlen += (int)n;


        char *start =
            inbuf;

        char *nl;


        while (
            (
                nl = memchr(
                    start,
                    '\n',
                    inlen - (start - inbuf)
                )
            )
            != NULL
        )
        {
            *nl = '\0';


            if (
                strncmp(
                    start,
                    "MSG:",
                    4
                ) == 0
            )
            {
                char *topic =
                    start + 4;


                char *sep =
                    strchr(
                        topic,
                        ':'
                    );


                if (sep != NULL)
                {
                    *sep = '\0';


                    printf(
                        ">> [%s] %s\n",
                        topic,
                        sep + 1
                    );
                }
            }


            start =
                nl + 1;
        }


        int remaining =
            inlen - (start - inbuf);


        memmove(
            inbuf,
            start,
            remaining
        );


        inlen =
            remaining;
    }


    /* ========================
       LIMPIEZA
       ======================== */

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