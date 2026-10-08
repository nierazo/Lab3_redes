/*
 * broker_quic.c
 * BONO: broker del sistema de noticias deportivas usando QUIC.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ============================
   SOCKETS: WINDOWS / LINUX
   ============================ */

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

#include "common.h"
#include "quic_common.h"


typedef struct {
    SSL *stream;
    char topic[TOPIC_MAX];
} subscription_t;


static subscription_t subs[MAX_SUBS];
static int n_subs = 0;

static pthread_mutex_t subs_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned char alpn_wire[64];
static unsigned int alpn_wire_len;


/* ============================
   ALPN
   ============================ */

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


static int alpn_select_cb(
    SSL *ssl,
    const unsigned char **out,
    unsigned char *outlen,
    const unsigned char *in,
    unsigned int inlen,
    void *arg
)
{
    (void)ssl;
    (void)arg;

    if (
        SSL_select_next_proto(
            (unsigned char **)out,
            outlen,
            alpn_wire,
            alpn_wire_len,
            in,
            inlen
        )
        != OPENSSL_NPN_NEGOTIATED
    )
    {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    return SSL_TLSEXT_ERR_OK;
}


/* ============================
   SUSCRIPCIONES
   ============================ */

static void add_subscription(
    SSL *stream,
    const char *topic
)
{
    pthread_mutex_lock(&subs_mutex);

    if (n_subs < MAX_SUBS)
    {
        strncpy(
            subs[n_subs].topic,
            topic,
            TOPIC_MAX - 1
        );

        subs[n_subs].topic[TOPIC_MAX - 1] = '\0';

        subs[n_subs].stream = stream;

        n_subs++;

        printf(
            "[broker] stream suscrito al tema '%s'\n",
            topic
        );
    }
    else
    {
        fprintf(
            stderr,
            "[broker] tabla de suscripciones llena\n"
        );
    }

    pthread_mutex_unlock(&subs_mutex);
}


static void remove_subscriptions_of(
    SSL *stream
)
{
    pthread_mutex_lock(&subs_mutex);

    int i = 0;

    while (i < n_subs)
    {
        if (subs[i].stream == stream)
        {
            subs[i] = subs[n_subs - 1];

            n_subs--;
        }
        else
        {
            i++;
        }
    }

    pthread_mutex_unlock(&subs_mutex);
}


/* ============================
   BROADCAST
   ============================ */

static void broadcast_to_topic(
    const char *topic,
    const char *line
)
{
    SSL *targets[MAX_SUBS];

    int n_targets = 0;

    pthread_mutex_lock(&subs_mutex);

    for (int i = 0; i < n_subs; i++)
    {
        if (
            strcmp(
                subs[i].topic,
                topic
            ) == 0
        )
        {
            targets[n_targets++] =
                subs[i].stream;
        }
    }

    pthread_mutex_unlock(&subs_mutex);


    for (int i = 0; i < n_targets; i++)
    {
        size_t written = 0;

        if (
            !SSL_write_ex(
                targets[i],
                line,
                strlen(line),
                &written
            )
        )
        {
            fprintf(
                stderr,
                "[broker] error enviando mensaje QUIC\n"
            );
        }
    }

    printf(
        "[broker] tema '%s': mensaje reenviado a %d suscriptor(es)\n",
        topic,
        n_targets
    );
}


/* ============================
   PROCESAR MENSAJE
   ============================ */

static void process_line(
    SSL *stream,
    char *line
)
{
    if (
        strncmp(
            line,
            "SUB:",
            4
        ) == 0
    )
    {
        add_subscription(
            stream,
            line + 4
        );
    }

    else if (
        strncmp(
            line,
            "MSG:",
            4
        ) == 0
    )
    {
        char *topic = line + 4;

        char *sep = strchr(
            topic,
            ':'
        );

        if (sep == NULL)
        {
            fprintf(
                stderr,
                "[broker] mensaje mal formado: %s\n",
                line
            );

            return;
        }

        *sep = '\0';

        char out[BUFFER_SIZE];

        snprintf(
            out,
            sizeof(out),
            "MSG:%s:%s\n",
            topic,
            sep + 1
        );

        broadcast_to_topic(
            topic,
            out
        );
    }

    else
    {
        fprintf(
            stderr,
            "[broker] comando desconocido: %s\n",
            line
        );
    }
}


/* ============================
   MANEJAR CLIENTE QUIC
   ============================ */

static void *handle_connection(
    void *arg
)
{
    SSL *conn = (SSL *)arg;

    SSL_set_blocking_mode(
        conn,
        1
    );

    SSL_set_incoming_stream_policy(
        conn,
        SSL_INCOMING_STREAM_POLICY_ACCEPT,
        0
    );


    SSL *stream =
        SSL_accept_stream(
            conn,
            0
        );

    if (!stream)
    {
        fprintf(
            stderr,
            "[broker] no se pudo aceptar el stream del cliente\n"
        );

        SSL_free(conn);

        return NULL;
    }


    SSL_set_blocking_mode(
        stream,
        1
    );

    printf(
        "[broker] stream aceptado, atendiendo cliente en un hilo dedicado\n"
    );


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
                "[broker] cliente supero el buffer de linea\n"
            );

            break;
        }


        size_t n = 0;


        if (
            SSL_read_ex(
                stream,
                inbuf + inlen,
                space,
                &n
            ) <= 0
            ||
            n == 0
        )
        {
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

            process_line(
                stream,
                start
            );

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


    printf(
        "[broker] cliente desconectado\n"
    );


    remove_subscriptions_of(
        stream
    );


    SSL_free(stream);

    SSL_free(conn);

    return NULL;
}


/* ============================
   MAIN
   ============================ */

int main(
    int argc,
    char *argv[]
)
{
    setvbuf(
        stdout,
        NULL,
        _IOLBF,
        0
    );


    if (argc != 2)
    {
        fprintf(
            stderr,
            "Uso: %s <puerto>\n",
            argv[0]
        );

        exit(1);
    }


    int port =
        atoi(argv[1]);


    /* ========================
       INICIALIZAR WINSOCK
       ======================== */

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
            "[broker] error inicializando Winsock\n"
        );

        return 1;
    }

#endif


    build_alpn_wire();


    /* ========================
       CONTEXTO QUIC
       ======================== */

    SSL_CTX *ctx =
        SSL_CTX_new(
            OSSL_QUIC_server_method()
        );


    if (!ctx)
    {
        ERR_print_errors_fp(
            stderr
        );

        exit(1);
    }


    /* ========================
       CERTIFICADO TLS
       ======================== */

    if (
        SSL_CTX_use_certificate_file(
            ctx,
            QUIC_CERT_FILE,
            SSL_FILETYPE_PEM
        ) <= 0

        ||

        SSL_CTX_use_PrivateKey_file(
            ctx,
            QUIC_KEY_FILE,
            SSL_FILETYPE_PEM
        ) <= 0
    )
    {
        fprintf(
            stderr,
            "[broker] no se pudo cargar %s/%s\n",
            QUIC_CERT_FILE,
            QUIC_KEY_FILE
        );

        ERR_print_errors_fp(
            stderr
        );

        exit(1);
    }


    SSL_CTX_set_alpn_select_cb(
        ctx,
        alpn_select_cb,
        NULL
    );


    /* ========================
       SOCKET UDP
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
            "[broker] error creando socket UDP: %d\n",
            WSAGetLastError()
        );

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

        exit(1);
    }

#endif


    struct sockaddr_in server_addr;


    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );


    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(port);


    /* ========================
       BIND
       ======================== */

#ifdef _WIN32

    if (
        bind(
            fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        )
        == SOCKET_ERROR
    )
    {
        fprintf(
            stderr,
            "[broker] bind fallo: %d\n",
            WSAGetLastError()
        );

        closesocket(fd);

        WSACleanup();

        return 1;
    }

#else

    if (
        bind(
            fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        )
        < 0
    )
    {
        perror("bind");

        exit(1);
    }

#endif


    /* ========================
       SOCKET NO BLOQUEANTE
       ======================== */

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
            "[broker] no se pudo poner el socket en modo no bloqueante\n"
        );

        closesocket(fd);

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
       LISTENER QUIC
       ======================== */

    SSL *listener =
        SSL_new_listener(
            ctx,
            0
        );


    if (!listener)
    {
        ERR_print_errors_fp(
            stderr
        );

        exit(1);
    }


    /*
     * OpenSSL documenta que SSL_set_fd también
     * puede usarse para QUIC; crea internamente
     * un BIO de datagramas.
     */

    if (
        !SSL_set_fd(
            listener,
            (int)fd
        )
    )
    {
        ERR_print_errors_fp(
            stderr
        );

        exit(1);
    }


    SSL_set_blocking_mode(
        listener,
        1
    );


    if (
        !SSL_listen(
            listener
        )
    )
    {
        ERR_print_errors_fp(
            stderr
        );

        exit(1);
    }


    printf(
        "[broker] escuchando en el puerto %d (QUIC/UDP)\n",
        port
    );


    /* ========================
       ACEPTAR CONEXIONES
       ======================== */

    while (1)
    {
        SSL *conn =
            SSL_accept_connection(
                listener,
                0
            );


        if (!conn)
        {
            fprintf(
                stderr,
                "[broker] accept_connection fallo\n"
            );

            ERR_print_errors_fp(
                stderr
            );

            continue;
        }


        printf(
            "[broker] nueva conexion QUIC aceptada\n"
        );


        pthread_t th;


        if (
            pthread_create(
                &th,
                NULL,
                handle_connection,
                conn
            )
            != 0
        )
        {
            fprintf(
                stderr,
                "[broker] error creando hilo\n"
            );

            SSL_free(conn);

            continue;
        }


        pthread_detach(
            th
        );
    }


#ifdef _WIN32

    closesocket(fd);

    WSACleanup();

#endif


    SSL_free(listener);

    SSL_CTX_free(ctx);


    return 0;
}