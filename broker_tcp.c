/* broker del sistema de noticias deportivas, version TCP.
 *
 * no distingue publishers de subscribers: el que manda "SUB:" queda
 * registrado en la tabla, el que manda "MSG:" dispara el reenvio.
 * usa select() para no tener que abrir un hilo por cliente.
 *
 * uso: ./broker_tcp <puerto>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* compatibilidad Windows / Linux */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET socket_t;

#define SOCKET_INVALIDO INVALID_SOCKET
#define CERRAR_SOCKET closesocket

#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

typedef int socket_t;

#define SOCKET_INVALIDO (-1)
#define CERRAR_SOCKET close
#endif

#include "common.h"


typedef struct {
    socket_t fd;               /* SOCKET_INVALIDO si el hueco esta libre */
    char buf[TAM_BUFER];       /* TCP es un flujo de bytes */
    int len;
} cliente_t;


typedef struct {
    socket_t fd;
    char tema[TEMA_MAX];
} suscripcion_t;


static cliente_t clientes[MAX_CLIENTES];
static suscripcion_t subs[MAX_SUBS];
static int n_subs = 0;


/* ---------------------------------------------------
   LOG AUXILIAR PARA MOSTRAR SOCKETS
   --------------------------------------------------- */

static unsigned long long valor_socket(socket_t fd) {
    return (unsigned long long)fd;
}


/* ---------------------------------------------------
   SUSCRIPCIONES
   --------------------------------------------------- */

static void agregar_suscripcion(
    socket_t fd,
    const char *tema
) {
    if (n_subs >= MAX_SUBS) {
        fprintf(
            stderr,
            "[broker] tabla de suscripciones llena\n"
        );
        return;
    }

    strncpy(
        subs[n_subs].tema,
        tema,
        TEMA_MAX - 1
    );

    subs[n_subs].tema[TEMA_MAX - 1] = '\0';

    subs[n_subs].fd = fd;

    n_subs++;

    printf(
        "[broker] fd=%llu suscrito al tema '%s'\n",
        valor_socket(fd),
        tema
    );
}


static void quitar_suscripciones_de(socket_t fd) {
    int i = 0;

    while (i < n_subs) {

        if (subs[i].fd == fd) {

            subs[i] =
                subs[n_subs - 1];

            n_subs--;

        } else {
            i++;
        }
    }
}


/* ---------------------------------------------------
   REENVIO
   --------------------------------------------------- */

static void reenviar_a_tema(
    const char *tema,
    const char *linea
) {
    int enviados = 0;

    for (int i = 0; i < n_subs; i++) {

        if (
            strcmp(
                subs[i].tema,
                tema
            ) == 0
        ) {

            if (
                send(
                    subs[i].fd,
                    linea,
                    (int)strlen(linea),
                    0
                ) >= 0
            ) {
                enviados++;
            }
        }
    }

    printf(
        "[broker] tema '%s': mensaje reenviado a %d suscriptor(es)\n",
        tema,
        enviados
    );
}


/* ---------------------------------------------------
   PROCESAR MENSAJE
   --------------------------------------------------- */

static void procesar_linea(
    socket_t fd,
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
            fd,
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
            "[broker] comando desconocido de fd=%llu: %s\n",
            valor_socket(fd),
            linea
        );
    }
}


/* ---------------------------------------------------
   BUFFER DE CLIENTE
   --------------------------------------------------- */

static void procesar_datos_cliente(
    cliente_t *c
) {
    char *inicio =
        c->buf;

    char *nl;

    while (
        (
            nl = memchr(
                inicio,
                '\n',
                c->len - (inicio - c->buf)
            )
        ) != NULL
    ) {

        *nl = '\0';

        procesar_linea(
            c->fd,
            inicio
        );

        inicio =
            nl + 1;
    }

    int restante =
        c->len - (inicio - c->buf);

    memmove(
        c->buf,
        inicio,
        restante
    );

    c->len =
        restante;
}


/* ---------------------------------------------------
   CERRAR CLIENTE
   --------------------------------------------------- */

static void cerrar_cliente(int hueco) {

    printf(
        "[broker] fd=%llu se desconecto\n",
        valor_socket(clientes[hueco].fd)
    );

    quitar_suscripciones_de(
        clientes[hueco].fd
    );

    CERRAR_SOCKET(
        clientes[hueco].fd
    );

    clientes[hueco].fd =
        SOCKET_INVALIDO;

    clientes[hueco].len =
        0;
}


/* ---------------------------------------------------
   MAIN
   --------------------------------------------------- */

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
            "[broker] error inicializando Winsock\n"
        );

        return 1;
    }

#endif


    for (
        int i = 0;
        i < MAX_CLIENTES;
        i++
    ) {
        clientes[i].fd =
            SOCKET_INVALIDO;

        clientes[i].len =
            0;
    }


    /* ------------------------------------------------
       CREAR SOCKET TCP
       ------------------------------------------------ */

    socket_t fd_escucha =
        socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );


    if (
        fd_escucha == SOCKET_INVALIDO
    ) {

#ifdef _WIN32
        fprintf(
            stderr,
            "[broker] error creando socket TCP: %d\n",
            WSAGetLastError()
        );

        WSACleanup();
#else
        perror("socket");
#endif

        return 1;
    }


    /* permitir reutilizar el puerto */
    int opt = 1;

#ifdef _WIN32

    setsockopt(
        fd_escucha,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char *)&opt,
        sizeof(opt)
    );

#else

    setsockopt(
        fd_escucha,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );

#endif


    struct sockaddr_in dir_servidor;

    memset(
        &dir_servidor,
        0,
        sizeof(dir_servidor)
    );


    dir_servidor.sin_family =
        AF_INET;

    dir_servidor.sin_addr.s_addr =
        INADDR_ANY;

    dir_servidor.sin_port =
        htons(puerto);


    /* ------------------------------------------------
       BIND
       ------------------------------------------------ */

    if (
        bind(
            fd_escucha,
            (struct sockaddr *)&dir_servidor,
            sizeof(dir_servidor)
        )
#ifdef _WIN32
        == SOCKET_ERROR
#else
        < 0
#endif
    ) {

#ifdef _WIN32
        fprintf(
            stderr,
            "[broker] bind fallo: %d\n",
            WSAGetLastError()
        );
#else
        perror("bind");
#endif

        CERRAR_SOCKET(fd_escucha);

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }


    /* ------------------------------------------------
       LISTEN
       ------------------------------------------------ */

    if (
        listen(
            fd_escucha,
            16
        )
#ifdef _WIN32
        == SOCKET_ERROR
#else
        < 0
#endif
    ) {

#ifdef _WIN32
        fprintf(
            stderr,
            "[broker] listen fallo: %d\n",
            WSAGetLastError()
        );
#else
        perror("listen");
#endif

        CERRAR_SOCKET(fd_escucha);

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }


    printf(
        "[broker] escuchando en el puerto %d (TCP)\n",
        puerto
    );


    /* ------------------------------------------------
       LOOP PRINCIPAL
       ------------------------------------------------ */

    while (1) {

        fd_set fds_lectura;

        FD_ZERO(
            &fds_lectura
        );

        FD_SET(
            fd_escucha,
            &fds_lectura
        );


#ifndef _WIN32
        socket_t fd_max =
            fd_escucha;
#endif


        for (
            int i = 0;
            i < MAX_CLIENTES;
            i++
        ) {

            if (
                clientes[i].fd !=
                SOCKET_INVALIDO
            ) {

                FD_SET(
                    clientes[i].fd,
                    &fds_lectura
                );

#ifndef _WIN32
                if (
                    clientes[i].fd >
                    fd_max
                ) {
                    fd_max =
                        clientes[i].fd;
                }
#endif
            }
        }


        /* En Windows el primer parametro de select()
         * se ignora. En Linux debe ser fd_max + 1.
         */

#ifdef _WIN32

        int resultado_select =
            select(
                0,
                &fds_lectura,
                NULL,
                NULL,
                NULL
            );

        if (
            resultado_select ==
            SOCKET_ERROR
        ) {

            fprintf(
                stderr,
                "[broker] select fallo: %d\n",
                WSAGetLastError()
            );

            continue;
        }

#else

        int resultado_select =
            select(
                fd_max + 1,
                &fds_lectura,
                NULL,
                NULL,
                NULL
            );

        if (
            resultado_select < 0
        ) {

            perror("select");

            continue;
        }

#endif


        /* ------------------------------------------------
           NUEVA CONEXION
           ------------------------------------------------ */

        if (
            FD_ISSET(
                fd_escucha,
                &fds_lectura
            )
        ) {

            struct sockaddr_in dir_cliente;


#ifdef _WIN32
            int len =
                sizeof(dir_cliente);
#else
            socklen_t len =
                sizeof(dir_cliente);
#endif


            socket_t nuevo_fd =
                accept(
                    fd_escucha,
                    (struct sockaddr *)&dir_cliente,
                    &len
                );


            if (
                nuevo_fd ==
                SOCKET_INVALIDO
            ) {

#ifdef _WIN32
                fprintf(
                    stderr,
                    "[broker] accept fallo: %d\n",
                    WSAGetLastError()
                );
#else
                perror("accept");
#endif

            } else {

                int hueco =
                    -1;


                for (
                    int i = 0;
                    i < MAX_CLIENTES;
                    i++
                ) {

                    if (
                        clientes[i].fd ==
                        SOCKET_INVALIDO
                    ) {

                        hueco = i;

                        break;
                    }
                }


                if (
                    hueco == -1
                ) {

                    fprintf(
                        stderr,
                        "[broker] limite de clientes alcanzado\n"
                    );

                    CERRAR_SOCKET(
                        nuevo_fd
                    );

                } else {

                    clientes[hueco].fd =
                        nuevo_fd;

                    clientes[hueco].len =
                        0;


                    printf(
                        "[broker] nueva conexion fd=%llu desde %s:%d\n",
                        valor_socket(nuevo_fd),
                        inet_ntoa(dir_cliente.sin_addr),
                        ntohs(dir_cliente.sin_port)
                    );
                }
            }
        }


        /* ------------------------------------------------
           DATOS DE CLIENTES EXISTENTES
           ------------------------------------------------ */

        for (
            int i = 0;
            i < MAX_CLIENTES;
            i++
        ) {

            socket_t fd =
                clientes[i].fd;


            if (
                fd == SOCKET_INVALIDO
                ||
                !FD_ISSET(
                    fd,
                    &fds_lectura
                )
            ) {
                continue;
            }


            int espacio =
                TAM_BUFER -
                clientes[i].len;


            if (
                espacio <= 0
            ) {

                fprintf(
                    stderr,
                    "[broker] fd=%llu supero el buffer de linea, se cierra\n",
                    valor_socket(fd)
                );

                cerrar_cliente(i);

                continue;
            }


            int n =
                recv(
                    fd,
                    clientes[i].buf +
                    clientes[i].len,
                    espacio,
                    0
                );


            if (
                n <= 0
            ) {

                cerrar_cliente(i);

            } else {

                clientes[i].len +=
                    n;

                procesar_datos_cliente(
                    &clientes[i]
                );
            }
        }
    }


    /* realmente no se alcanza por el while(1),
     * pero queda la limpieza correcta.
     */

    CERRAR_SOCKET(
        fd_escucha
    );


#ifdef _WIN32
    WSACleanup();
#endif


    return 0;
}