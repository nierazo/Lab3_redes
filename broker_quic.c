/*
 * Broker de publicacion/suscripcion usando QUIC.
 *
 * Recibe mensajes de los clientes y los reenvia a los que esten
 * suscritos al tema correspondiente.
 *
 * Comandos:
 *   SUB:tema          Suscribirse a un tema
 *   MSG:tema:mensaje  Publicar un mensaje
 *
 * QUIC funciona sobre UDP, pero ofrece streams fiables y ordenados.
 * En este caso se usa OpenSSL para manejar la conexion y TLS 1.3.
 *
 * Como la lectura y escritura se hacen en modo bloqueante, cada
 * conexion se atiende en un hilo independiente.
 *
 * Antes de ejecutar el broker, generar los certificados con:
 *   make certs
 *
 * Uso:
 *   ./broker_quic <puerto>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <openssl/ssl.h>
#include <openssl/quic.h>
#include <openssl/err.h>

#include "common.h"
#include "quic_common.h"


/*
 * Guarda el stream de cada cliente junto con el tema al que
 * se suscribio. Un cliente puede tener varias suscripciones.
 */
typedef struct {
    SSL *stream;
    char tema[TEMA_MAX];
} suscripcion_t;


/* Lista compartida de suscripciones. */
static suscripcion_t suscripciones[MAX_SUBS];
static int num_suscripciones = 0;

/*
 * Varios hilos pueden modificar o consultar las suscripciones
 * al mismo tiempo, por eso se protege la lista con un mutex.
 */
static pthread_mutex_t mutex_suscripciones = PTHREAD_MUTEX_INITIALIZER;


/*
 * ALPN permite que cliente y servidor acuerden el protocolo
 * de aplicacion que van a utilizar durante el handshake TLS.
 *
 * OpenSSL espera cada protocolo precedido por un byte que
 * indique su longitud, no como una cadena C convencional.
 */
static unsigned char protocolos_alpn[64];
static unsigned int longitud_alpn;


static void configurar_alpn(void) {
    size_t longitud = strlen(QUIC_ALPN_PROTO);

    protocolos_alpn[0] = (unsigned char)longitud;
    memcpy(protocolos_alpn + 1, QUIC_ALPN_PROTO, longitud);

    longitud_alpn = (unsigned int)(longitud + 1);
}


/*
 * Selecciona el protocolo ALPN que ofrece el cliente.
 * El broker solo acepta conexiones que usen QUIC_ALPN_PROTO.
 */
static int seleccionar_alpn(
    SSL *ssl,
    const unsigned char **protocolo,
    unsigned char *longitud,
    const unsigned char *protocolos_cliente,
    unsigned int longitud_cliente,
    void *arg
) {
    (void)ssl;
    (void)arg;

    /*
     * SSL_select_next_proto compara las listas de protocolos
     * del servidor y del cliente.
     */
    if (SSL_select_next_proto(
            (unsigned char **)protocolo,
            longitud,
            protocolos_alpn,
            longitud_alpn,
            protocolos_cliente,
            longitud_cliente
        ) != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }

    return SSL_TLSEXT_ERR_OK;
}


/* Agrega una suscripcion a la lista compartida. */
static void agregar_suscripcion(SSL *stream, const char *tema) {
    pthread_mutex_lock(&mutex_suscripciones);

    if (num_suscripciones < MAX_SUBS) {
        suscripcion_t *nueva = &suscripciones[num_suscripciones];

        /*
         * Se limita la copia al tamaño del arreglo y se agrega
         * '\0' para asegurar que el tema termine correctamente.
         */
        strncpy(nueva->tema, tema, TEMA_MAX - 1);
        nueva->tema[TEMA_MAX - 1] = '\0';
        nueva->stream = stream;

        num_suscripciones++;
    } else {
        fprintf(stderr, "[broker] tabla de suscripciones llena\n");
    }

    pthread_mutex_unlock(&mutex_suscripciones);

    printf("[broker] suscripcion al tema '%s'\n", tema);
}


/*
 * Elimina todas las suscripciones asociadas a un stream.
 * Se reemplaza cada elemento eliminado por el ultimo de la lista,
 * ya que no necesitamos conservar el orden de las suscripciones.
 */
static void quitar_suscripciones(SSL *stream) {
    pthread_mutex_lock(&mutex_suscripciones);

    int i = 0;

    while (i < num_suscripciones) {
        if (suscripciones[i].stream == stream) {
            suscripciones[i] = suscripciones[num_suscripciones - 1];
            num_suscripciones--;
        } else {
            i++;
        }
    }

    pthread_mutex_unlock(&mutex_suscripciones);
}


/*
 * Reenvia un mensaje a todos los clientes suscritos al tema.
 *
 * Primero se guardan los streams que coinciden y se libera el
 * mutex antes de escribir. La escritura puede bloquearse y no
 * queremos impedir que otros hilos consulten las suscripciones
 * mientras un cliente recibe sus datos.
 */
static void reenviar_mensaje(const char *tema, const char *mensaje) {
    SSL *destinos[MAX_SUBS];
    int num_destinos = 0;

    pthread_mutex_lock(&mutex_suscripciones);

    for (int i = 0; i < num_suscripciones; i++) {
        if (strcmp(suscripciones[i].tema, tema) == 0) {
            destinos[num_destinos] = suscripciones[i].stream;
            num_destinos++;
        }
    }

    pthread_mutex_unlock(&mutex_suscripciones);

    for (int i = 0; i < num_destinos; i++) {
        size_t bytes_escritos = 0;

        /*
         * SSL_write_ex envia los datos por el stream QUIC.
         * Se mantiene el mismo formato de mensajes que en TCP
         * para poder comparar los transportes.
         */
        if (SSL_write_ex(
                destinos[i],
                mensaje,
                strlen(mensaje),
                &bytes_escritos
            ) != 1) {
            fprintf(stderr, "[broker] error al reenviar mensaje\n");
        }
    }

    printf(
        "[broker] tema '%s': mensaje enviado a %d suscriptor(es)\n",
        tema,
        num_destinos
    );
}


/* Interpreta y procesa una linea recibida de un cliente. */
static void procesar_mensaje(SSL *stream, char *linea) {
    if (strncmp(linea, "SUB:", 4) == 0) {
        agregar_suscripcion(stream, linea + 4);

    } else if (strncmp(linea, "MSG:", 4) == 0) {
        char *tema = linea + 4;
        char *separador = strchr(tema, ':');

        /*
         * El primer ':' despues de MSG: separa el tema
         * del contenido del mensaje.
         */
        if (separador == NULL) {
            fprintf(stderr, "[broker] mensaje mal formado: %s\n", linea);
            return;
        }

        *separador = '\0';

        char mensaje[TAM_BUFER];

        snprintf(
            mensaje,
            sizeof(mensaje),
            "MSG:%s:%s\n",
            tema,
            separador + 1
        );

        reenviar_mensaje(tema, mensaje);

    } else {
        fprintf(stderr, "[broker] comando desconocido: %s\n", linea);
    }
}


/*
 * Atiende una conexion QUIC en un hilo separado.
 *
 * Se acepta el primer stream entrante del cliente y se procesan
 * sus datos como lineas terminadas en '\n'. Aunque QUIC usa UDP
 * como transporte, sus streams ofrecen entrega fiable y ordenada,
 * por lo que se puede usar el mismo protocolo de lineas que en TCP.
 */
static void *atender_cliente(void *arg) {
    SSL *conexion = (SSL *)arg;

    SSL_set_blocking_mode(conexion, 1);

    /*
     * Se permite aceptar streams iniciados por el cliente.
     * El broker usa el primero para intercambiar los mensajes.
     */
    SSL_set_incoming_stream_policy(
        conexion,
        SSL_INCOMING_STREAM_POLICY_ACCEPT,
        0
    );

    SSL *stream = SSL_accept_stream(conexion, 0);

    if (stream == NULL) {
        fprintf(stderr, "[broker] no se pudo aceptar el stream\n");
        SSL_free(conexion);
        return NULL;
    }

    SSL_set_blocking_mode(stream, 1);

    printf("[broker] stream aceptado\n");

    char buffer[TAM_BUFER];
    int bytes_buffer = 0;

    while (1) {
        int espacio = TAM_BUFER - bytes_buffer;

        /*
         * Si el buffer se llena sin encontrar un salto de linea,
         * no cabe otra lectura. Se cierra la conexion para evitar
         * procesar una linea incompleta o desbordar el buffer.
         */
        if (espacio <= 0) {
            fprintf(stderr, "[broker] linea demasiado larga\n");
            break;
        }

        size_t bytes_leidos = 0;

        if (SSL_read_ex(
                stream,
                buffer + bytes_buffer,
                (size_t)espacio,
                &bytes_leidos
            ) != 1 || bytes_leidos == 0) {
            break;
        }

        bytes_buffer += (int)bytes_leidos;

        /*
         * Una lectura no necesariamente contiene una linea completa:
         * puede traer varias o dejar parte de una para la siguiente
         * lectura. Por eso se procesan solo las lineas con '\n'.
         */
        char *inicio = buffer;
        char *salto_linea;

        while ((salto_linea = memchr(
                    inicio,
                    '\n',
                    (size_t)(buffer + bytes_buffer - inicio)
                )) != NULL) {

            *salto_linea = '\0';
            procesar_mensaje(stream, inicio);

            inicio = salto_linea + 1;
        }

        /*
         * Se mueve al principio del buffer cualquier fragmento
         * que todavia no forme una linea completa.
         */
        int bytes_restantes = (int)(buffer + bytes_buffer - inicio);

        memmove(buffer, inicio, (size_t)bytes_restantes);
        bytes_buffer = bytes_restantes;
    }

    printf("[broker] cliente desconectado\n");

    quitar_suscripciones(stream);

    SSL_free(stream);
    SSL_free(conexion);

    return NULL;
}


int main(int argc, char *argv[]) {
    /*
     * Hace que los mensajes de la consola se muestren por linea,
     * algo util para observar lo que ocurre con varios hilos.
     */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return 1;
    }

    int puerto = atoi(argv[1]);

    configurar_alpn();

    /*
     * Se crea el contexto TLS para un servidor QUIC.
     * QUIC requiere TLS 1.3, por lo que se necesitan el certificado
     * y la clave privada del servidor.
     */
    SSL_CTX *contexto = SSL_CTX_new(OSSL_QUIC_server_method());

    if (contexto == NULL) {
        ERR_print_errors_fp(stderr);
        return 1;
    }

    if (SSL_CTX_use_certificate_file(
            contexto,
            QUIC_ARCHIVO_CERT,
            SSL_FILETYPE_PEM
        ) <= 0 ||
        SSL_CTX_use_PrivateKey_file(
            contexto,
            QUIC_ARCHIVO_CLAVE,
            SSL_FILETYPE_PEM
        ) <= 0) {

        fprintf(
            stderr,
            "[broker] no se pudieron cargar el certificado y la clave. "
            "Ejecute 'make certs'\n"
        );

        ERR_print_errors_fp(stderr);
        SSL_CTX_free(contexto);
        return 1;
    }

    SSL_CTX_set_alpn_select_cb(contexto, seleccionar_alpn, NULL);

    /*
     * QUIC utiliza UDP. OpenSSL se encarga del protocolo QUIC
     * sobre este socket, por lo que el descriptor debe ser
     * no bloqueante para el manejo interno de los datagramas.
     */
    int socket_udp = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_udp < 0) {
        perror("socket");
        SSL_CTX_free(contexto);
        return 1;
    }

    struct sockaddr_in direccion;
    memset(&direccion, 0, sizeof(direccion));

    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = htonl(INADDR_ANY);
    direccion.sin_port = htons((unsigned short)puerto);

    if (bind(
            socket_udp,
            (struct sockaddr *)&direccion,
            sizeof(direccion)
        ) < 0) {

        perror("bind");
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    int flags = fcntl(socket_udp, F_GETFL, 0);

    if (flags < 0 || fcntl(socket_udp, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl");
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    /*
     * El listener recibe las conexiones QUIC entrantes.
     * A partir de aqui, OpenSSL se encarga del establecimiento
     * de las conexiones y del handshake correspondiente.
     */
    SSL *listener = SSL_new_listener(contexto, 0);

    if (listener == NULL) {
        ERR_print_errors_fp(stderr);
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    if (SSL_set_fd(listener, socket_udp) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_free(listener);
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    SSL_set_blocking_mode(listener, 1);

    if (SSL_listen(listener) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_free(listener);
        close(socket_udp);
        SSL_CTX_free(contexto);
        return 1;
    }

    printf("[broker] escuchando en el puerto %d (QUIC/UDP)\n", puerto);

    while (1) {
        SSL *conexion = SSL_accept_connection(listener, 0);

        if (conexion == NULL) {
            fprintf(stderr, "[broker] fallo al aceptar una conexion\n");
            ERR_print_errors_fp(stderr);
            continue;
        }

        printf("[broker] nueva conexion QUIC\n");

        pthread_t hilo;

        if (pthread_create(
                &hilo,
                NULL,
                atender_cliente,
                conexion
            ) != 0) {

            fprintf(stderr, "[broker] no se pudo crear el hilo\n");
            SSL_free(conexion);
            continue;
        }

        /*
         * El hilo se desacopla porque el servidor no necesita
         * esperar a que termine. Cada hilo libera sus recursos
         * cuando el cliente se desconecta.
         */
        pthread_detach(hilo);
    }

    /* El servidor normalmente permanece en el ciclo anterior. */
    SSL_free(listener);
    close(socket_udp);
    SSL_CTX_free(contexto);

    return 0;
}
