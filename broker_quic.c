/* bono: el mismo broker pero sobre QUIC en vez de TCP/UDP crudo.
 * usa la implementacion de QUIC que ya trae OpenSSL >= 3.5 en libssl
 * (openssl/quic.h), que por debajo sigue siendo un socket UDP normal
 * que este programa crea y administra.
 *
 * el protocolo de aplicacion es el mismo SUB:/MSG: de siempre, para
 * poder comparar los tres transportes en igualdad de condiciones.
 *
 * como QUIC exige TLS 1.3, el broker necesita certificado (quic_common.h
 * + "make certs"). y como la API bloqueante de OpenSSL para QUIC no se
 * lleva bien con select(), aqui cada conexion se atiende en su propio
 * hilo en vez de multiplexar todo en uno solo como hace broker_tcp.c.
 *
 * uso: ./broker_quic <puerto>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* compatibilidad Windows / Linux */
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
    SSL *stream;              /* stream QUIC por el que se le reenvian mensajes */
    char tema[TEMA_MAX];
} suscripcion_t;


static suscripcion_t subs[MAX_SUBS];
static int n_subs = 0;

static pthread_mutex_t mutex_subs =
    PTHREAD_MUTEX_INITIALIZER;


static unsigned char buf_alpn[64];
static unsigned int len_alpn;


/* arma el buffer de ALPN como lo pide TLS: un byte de longitud
 * seguido del nombre del protocolo (ver SSL_CTX_set_alpn_protos)
 */
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


/* callback de ALPN: el broker solo acepta el protocolo que definimos
 * en quic_common.h, cualquier otra cosa que ofrezca el cliente se rechaza
 */
static int seleccionar_alpn(
    SSL *ssl,
    const unsigned char **out,
    unsigned char *outlen,
    const unsigned char *in,
    unsigned int inlen,
    void *arg
) {

    (void)ssl;
    (void)arg;

    if (
        SSL_select_next_proto(
            (unsigned char **)out,
            outlen,
            buf_alpn,
            len_alpn,
            in,
            inlen
        )
        != OPENSSL_NPN_NEGOTIATED
    ) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    return SSL_TLSEXT_ERR_OK;
}


static void agregar_suscripcion(
    SSL *stream,
    const char *tema
) {

    pthread_mutex_lock(&mutex_subs);

    if (n_subs < MAX_SUBS) {

        strncpy(
            subs[n_subs].tema,
            tema,
            TEMA_MAX - 1
        );

        subs[n_subs].tema[TEMA_MAX - 1] = '\0';

        subs[n_subs].stream = stream;

        n_subs++;

    } else {

        fprintf(
            stderr,
            "[broker] tabla de suscripciones llena\n"
        );
    }

    pthread_mutex_unlock(&mutex_subs);

    printf(
        "[broker] stream suscrito al tema '%s'\n",
        tema
    );
}


static void quitar_suscripciones_de(
    SSL *stream
) {

    pthread_mutex_lock(&mutex_subs);

    int i = 0;

    while (i < n_subs) {

        if (subs[i].stream == stream) {

            subs[i] = subs[n_subs - 1];

            n_subs--;

        } else {

            i++;
        }
    }

    pthread_mutex_unlock(&mutex_subs);
}


/* copia la lista de streams suscritos bajo el mutex y manda los
 * mensajes ya afuera de la seccion critica (el envio bloquea)
 */
static void reenviar_a_tema(
    const char *tema,
    const char *linea
) {

    SSL *destinos[MAX_SUBS];

    int n_destinos = 0;


    pthread_mutex_lock(&mutex_subs);

    for (int i = 0; i < n_subs; i++) {

        if (
            strcmp(
                subs[i].tema,
                tema
            ) == 0
        ) {

            destinos[n_destinos++] =
                subs[i].stream;
        }
    }

    pthread_mutex_unlock(&mutex_subs);


    for (int i = 0; i < n_destinos; i++) {

        size_t escritos = 0;

        SSL_write_ex(
            destinos[i],
            linea,
            strlen(linea),
            &escritos
        );
    }


    printf(
        "[broker] tema '%s': mensaje reenviado a %d suscriptor(es)\n",
        tema,
        n_destinos
    );
}


static void procesar_linea(
    SSL *stream,
    char *linea
) {

    if (
        strncmp(
            linea,
            "SUB:",
            4
        ) == 0
    ) {

        agregar_suscripcion(
            stream,
            linea + 4
        );
    }

    else if (
        strncmp(
            linea,
            "MSG:",
            4
        ) == 0
    ) {

        char *tema =
            linea + 4;

        char *sep =
            strchr(
                tema,
                ':'
            );


        if (sep == NULL) {

            fprintf(
                stderr,
                "[broker] mensaje mal formado: %s\n",
                linea
            );

            return;
        }


        *sep = '\0';


        char salida[TAM_BUFER];


        snprintf(
            salida,
            sizeof(salida),
            "MSG:%s:%s\n",
            tema,
            sep + 1
        );


        reenviar_a_tema(
            tema,
            salida
        );
    }

    else {

        fprintf(
            stderr,
            "[broker] comando desconocido: %s\n",
            linea
        );
    }
}


/* un hilo por conexion QUIC aceptada. se toma el primer stream que
 * abre el cliente y se trata como un flujo de lineas, igual que una
 * conexion TCP (un stream QUIC tambien es bytes fiables y en orden,
 * asi que sirve el mismo truco de buscar '\n' para armar las lineas)
 */
static void *atender_conexion(
    void *arg
) {

    SSL *conn =
        (SSL *)arg;


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


    if (!stream) {

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


    char buf_rx[TAM_BUFER];

    int len_rx = 0;


    while (1) {

        int espacio =
            TAM_BUFER - len_rx;


        if (espacio <= 0) {

            fprintf(
                stderr,
                "[broker] cliente supero el buffer de linea, se cierra\n"
            );

            break;
        }


        size_t n = 0;


        if (
            SSL_read_ex(
                stream,
                buf_rx + len_rx,
                espacio,
                &n
            ) <= 0
            ||
            n == 0
        ) {

            break;
        }


        len_rx += (int)n;


        char *inicio =
            buf_rx;

        char *nl;


        while (
            (
                nl = memchr(
                    inicio,
                    '\n',
                    len_rx - (inicio - buf_rx)
                )
            )
            != NULL
        ) {

            *nl = '\0';


            procesar_linea(
                stream,
                inicio
            );


            inicio =
                nl + 1;
        }


        int restante =
            len_rx - (inicio - buf_rx);


        memmove(
            buf_rx,
            inicio,
            restante
        );


        len_rx =
            restante;
    }


    printf(
        "[broker] cliente desconectado\n"
    );


    quitar_suscripciones_de(
        stream
    );


    SSL_free(stream);

    SSL_free(conn);


    return NULL;
}


int main(
    int argc,
    char *argv[]
) {

    setvbuf(
        stdout,
        NULL,
        _IOLBF,
        0
    );


    if (argc != 2) {

        fprintf(
            stderr,
            "Uso: %s <puerto>\n",
            argv[0]
        );

        exit(1);
    }


    int puerto =
        atoi(argv[1]);


    /* Windows necesita inicializar Winsock antes de usar sockets */
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
            "[broker] error inicializando Winsock\n"
        );

        return 1;
    }

#endif


    armar_alpn();


    SSL_CTX *ctx =
        SSL_CTX_new(
            OSSL_QUIC_server_method()
        );


    if (!ctx) {

        ERR_print_errors_fp(stderr);

        exit(1);
    }


    if (
        SSL_CTX_use_certificate_file(
            ctx,
            QUIC_ARCHIVO_CERT,
            SSL_FILETYPE_PEM
        ) <= 0

        ||

        SSL_CTX_use_PrivateKey_file(
            ctx,
            QUIC_ARCHIVO_CLAVE,
            SSL_FILETYPE_PEM
        ) <= 0
    ) {

        fprintf(
            stderr,
            "[broker] no se pudo cargar %s/%s (ejecute 'make certs')\n",
            QUIC_ARCHIVO_CERT,
            QUIC_ARCHIVO_CLAVE
        );

        ERR_print_errors_fp(stderr);

        exit(1);
    }


    SSL_CTX_set_alpn_select_cb(
        ctx,
        seleccionar_alpn,
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
            "[broker] error creando socket UDP: %d\n",
            WSAGetLastError()
        );

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

        exit(1);
    }

#endif


    struct sockaddr_in dir_local;


    memset(
        &dir_local,
        0,
        sizeof(dir_local)
    );


    dir_local.sin_family =
        AF_INET;

    dir_local.sin_addr.s_addr =
        INADDR_ANY;

    dir_local.sin_port =
        htons(puerto);


    /* asociar el socket al puerto */
#ifdef _WIN32

    if (
        bind(
            fd,
            (struct sockaddr *)&dir_local,
            sizeof(dir_local)
        )
        == SOCKET_ERROR
    ) {

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
            (struct sockaddr *)&dir_local,
            sizeof(dir_local)
        )
        < 0
    ) {

        perror("bind");

        exit(1);
    }

#endif


    /* OpenSSL espera los datagramas por su cuenta,
     * para eso necesita que el socket quede no bloqueante.
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


    SSL *listener =
        SSL_new_listener(
            ctx,
            0
        );


    if (!listener) {

        ERR_print_errors_fp(stderr);

        exit(1);
    }


    SSL_set_fd(
        listener,
        (int)fd
    );


    SSL_set_blocking_mode(
        listener,
        1
    );


    if (
        !SSL_listen(
            listener
        )
    ) {

        ERR_print_errors_fp(stderr);

        exit(1);
    }


    printf(
        "[broker] escuchando en el puerto %d (QUIC/UDP)\n",
        puerto
    );


    while (1) {

        SSL *conn =
            SSL_accept_connection(
                listener,
                0
            );


        if (!conn) {

            fprintf(
                stderr,
                "[broker] accept_connection fallo\n"
            );

            ERR_print_errors_fp(stderr);

            continue;
        }


        printf(
            "[broker] nueva conexion QUIC aceptada\n"
        );


        pthread_t hilo;


        if (
            pthread_create(
                &hilo,
                NULL,
                atender_conexion,
                conn
            )
            != 0
        ) {

            perror(
                "pthread_create"
            );

            SSL_free(conn);

            continue;
        }


        pthread_detach(
            hilo
        );
    }


    return 0;
}