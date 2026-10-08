/* broker version UDP: aqui no hay conexiones, solo datagramas sueltos.
 * segun lo que traiga cada uno, se registra una direccion como
 * suscrita a un tema o se reenvia un mensaje a todas las direcciones
 * suscritas a ese tema.
 *
 * uso: ./broker_udp <puerto>
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
#include <netinet/in.h>

typedef int socket_t;

#define SOCKET_INVALIDO (-1)
#define CERRAR_SOCKET close
#endif

#include "common.h"


typedef struct {
    struct sockaddr_in dir;
    char tema[TEMA_MAX];
} suscripcion_t;


static suscripcion_t subs[MAX_SUBS];
static int n_subs = 0;


/* comprueba si dos direcciones representan el mismo cliente */
static int misma_direccion(
    const struct sockaddr_in *a,
    const struct sockaddr_in *b
) {
    return
        a->sin_addr.s_addr == b->sin_addr.s_addr &&
        a->sin_port == b->sin_port;
}


/* registra una direccion como suscrita a un tema */
static void agregar_suscripcion(
    const struct sockaddr_in *dir,
    const char *tema
) {

    /* evitar suscripciones repetidas */
    for (int i = 0; i < n_subs; i++) {

        if (
            misma_direccion(
                &subs[i].dir,
                dir
            )
            &&
            strcmp(
                subs[i].tema,
                tema
            ) == 0
        ) {
            return;
        }
    }


    if (n_subs >= MAX_SUBS) {

        fprintf(
            stderr,
            "[broker] tabla de suscripciones llena\n"
        );

        return;
    }


    subs[n_subs].dir =
        *dir;


    strncpy(
        subs[n_subs].tema,
        tema,
        TEMA_MAX - 1
    );


    subs[n_subs].tema[TEMA_MAX - 1] =
        '\0';


    n_subs++;


    printf(
        "[broker] %s:%d suscrito al tema '%s'\n",
        inet_ntoa(dir->sin_addr),
        ntohs(dir->sin_port),
        tema
    );
}


/* reenvia un datagrama a todos los clientes suscritos al tema */
static void reenviar_a_tema(
    socket_t sock,
    const char *tema,
    const char *mensaje,
    int len_mensaje
) {

    int enviados = 0;


    for (int i = 0; i < n_subs; i++) {

        if (
            strcmp(
                subs[i].tema,
                tema
            ) == 0
        ) {

            int resultado =
                sendto(
                    sock,
                    mensaje,
                    len_mensaje,
                    0,
                    (struct sockaddr *)&subs[i].dir,
                    sizeof(subs[i].dir)
                );


#ifdef _WIN32
            if (resultado != SOCKET_ERROR) {
                enviados++;
            }
#else
            if (resultado >= 0) {
                enviados++;
            }
#endif
        }
    }


    printf(
        "[broker] tema '%s': mensaje reenviado a %d suscriptor(es)\n",
        tema,
        enviados
    );
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


    /* crear socket UDP */
    socket_t sock =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );


    if (
        sock == SOCKET_INVALIDO
    ) {

#ifdef _WIN32

        fprintf(
            stderr,
            "[broker] error creando socket UDP: %d\n",
            WSAGetLastError()
        );

        WSACleanup();

#else

        perror("socket");

#endif

        return 1;
    }


    /* direccion local del broker */
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


    /* asociar socket al puerto */
    if (
        bind(
            sock,
            (struct sockaddr *)&dir_local,
            sizeof(dir_local)
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

        CERRAR_SOCKET(sock);

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }


    printf(
        "[broker] escuchando en el puerto %d (UDP)\n",
        puerto
    );


    char datagrama[TAM_BUFER];


    while (1) {

        struct sockaddr_in dir_remitente;


#ifdef _WIN32
        int len =
            sizeof(dir_remitente);
#else
        socklen_t len =
            sizeof(dir_remitente);
#endif


        int n =
            recvfrom(
                sock,
                datagrama,
                TAM_BUFER - 1,
                0,
                (struct sockaddr *)&dir_remitente,
                &len
            );


#ifdef _WIN32

        if (
            n == SOCKET_ERROR
        ) {
            continue;
        }

#else

        if (
            n <= 0
        ) {
            continue;
        }

#endif


        datagrama[n] =
            '\0';


        /* suscripcion */
        if (
            strncmp(
                datagrama,
                "SUB:",
                4
            ) == 0
        ) {

            agregar_suscripcion(
                &dir_remitente,
                datagrama + 4
            );
        }


        /* mensaje de publisher */
        else if (
            strncmp(
                datagrama,
                "MSG:",
                4
            ) == 0
        ) {

            char *inicio_tema =
                datagrama + 4;


            char *sep =
                strchr(
                    inicio_tema,
                    ':'
                );


            if (
                sep == NULL
            ) {

                fprintf(
                    stderr,
                    "[broker] mensaje mal formado: %s\n",
                    datagrama
                );

                continue;
            }


            int len_tema =
                (int)(sep - inicio_tema);


            if (
                len_tema >= TEMA_MAX
            ) {

                len_tema =
                    TEMA_MAX - 1;
            }


            char tema[TEMA_MAX];


            memcpy(
                tema,
                inicio_tema,
                len_tema
            );


            tema[len_tema] =
                '\0';


            /* el datagrama se reenvia exactamente como llego */
            reenviar_a_tema(
                sock,
                tema,
                datagrama,
                n
            );
        }


        else {

            fprintf(
                stderr,
                "[broker] comando desconocido: %s\n",
                datagrama
            );
        }
    }


    CERRAR_SOCKET(sock);


#ifdef _WIN32
    WSACleanup();
#endif


    return 0;
}