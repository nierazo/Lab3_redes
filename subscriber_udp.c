/* subscriber que registra su interes en uno o varios partidos mandando
 * un datagrama SUB por cada uno, y se queda escuchando lo que el
 * broker le reenvie por UDP.
 *
 * uso: ./subscriber_udp <ip_broker> <puerto> <tema1> [tema2 ...]
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

typedef int socket_t;

#define SOCKET_INVALIDO (-1)
#define CERRAR_SOCKET close

#endif

#include "common.h"


int main(int argc, char *argv[]) {

    setvbuf(
        stdout,
        NULL,
        _IOLBF,
        0
    );


    if (argc < 4) {

        fprintf(
            stderr,
            "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n",
            argv[0]
        );

        exit(1);
    }


    const char *ip_broker =
        argv[1];

    int puerto =
        atoi(argv[2]);


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
            "[subscriber] error inicializando Winsock\n"
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
            "[subscriber] error creando socket UDP: %d\n",
            WSAGetLastError()
        );

        WSACleanup();

#else

        perror("socket");

#endif

        return 1;
    }


    /* direccion del broker */
    struct sockaddr_in dir_broker;


    memset(
        &dir_broker,
        0,
        sizeof(dir_broker)
    );


    dir_broker.sin_family =
        AF_INET;

    dir_broker.sin_port =
        htons(puerto);


    if (
        inet_pton(
            AF_INET,
            ip_broker,
            &dir_broker.sin_addr
        )
        <= 0
    ) {

        fprintf(
            stderr,
            "Direccion IP invalida: %s\n",
            ip_broker
        );

        CERRAR_SOCKET(sock);

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }


    /* mandar una suscripcion por cada tema */
    for (int i = 3; i < argc; i++) {

        char linea[TAM_BUFER];


        int len =
            snprintf(
                linea,
                sizeof(linea),
                "SUB:%s",
                argv[i]
            );


        int resultado =
            sendto(
                sock,
                linea,
                len,
                0,
                (struct sockaddr *)&dir_broker,
                sizeof(dir_broker)
            );


#ifdef _WIN32

        if (
            resultado == SOCKET_ERROR
        ) {

            fprintf(
                stderr,
                "[subscriber] error enviando suscripcion: %d\n",
                WSAGetLastError()
            );

            continue;
        }

#else

        if (
            resultado < 0
        ) {

            perror("sendto");

            continue;
        }

#endif


        printf(
            "[subscriber] suscrito al tema '%s'\n",
            argv[i]
        );
    }


    printf(
        "[subscriber] esperando actualizaciones...\n"
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


        if (
            strncmp(
                datagrama,
                "MSG:",
                4
            ) == 0
        ) {

            char *tema =
                datagrama + 4;


            char *sep =
                strchr(
                    tema,
                    ':'
                );


            if (
                sep != NULL
            ) {

                *sep = '\0';


                printf(
                    ">> [%s] %s\n",
                    tema,
                    sep + 1
                );
            }
        }
    }


    CERRAR_SOCKET(sock);


#ifdef _WIN32
    WSACleanup();
#endif


    return 0;
}