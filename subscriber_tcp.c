/* subscriber (hincha) que se conecta al broker por TCP, se suscribe a
 * uno o varios partidos y va mostrando en pantalla lo que le llega.
 *
 * uso: ./subscriber_tcp <ip_broker> <puerto> <tema1> [tema2 ...]
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


    const char *ip_servidor =
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


    /* crear socket TCP */
    socket_t sock =
        socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );


    if (
        sock == SOCKET_INVALIDO
    ) {

#ifdef _WIN32

        fprintf(
            stderr,
            "[subscriber] error creando socket TCP: %d\n",
            WSAGetLastError()
        );

        WSACleanup();

#else

        perror("socket");

#endif

        return 1;
    }


    /* direccion del broker */
    struct sockaddr_in dir_servidor;


    memset(
        &dir_servidor,
        0,
        sizeof(dir_servidor)
    );


    dir_servidor.sin_family =
        AF_INET;

    dir_servidor.sin_port =
        htons(puerto);


    if (
        inet_pton(
            AF_INET,
            ip_servidor,
            &dir_servidor.sin_addr
        )
        <= 0
    ) {

        fprintf(
            stderr,
            "Direccion IP invalida: %s\n",
            ip_servidor
        );

        CERRAR_SOCKET(sock);

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }


    /* conectar al broker */
    int resultado =
        connect(
            sock,
            (struct sockaddr *)&dir_servidor,
            sizeof(dir_servidor)
        );


#ifdef _WIN32

    if (
        resultado == SOCKET_ERROR
    ) {

        fprintf(
            stderr,
            "[subscriber] connect fallo: %d\n",
            WSAGetLastError()
        );

        CERRAR_SOCKET(sock);

        WSACleanup();

        return 1;
    }

#else

    if (
        resultado < 0
    ) {

        perror("connect");

        CERRAR_SOCKET(sock);

        return 1;
    }

#endif


    printf(
        "[subscriber] conectado al broker %s:%d\n",
        ip_servidor,
        puerto
    );


    /* mandar una suscripcion por cada tema */
    for (int i = 3; i < argc; i++) {

        char linea[TAM_BUFER];


        snprintf(
            linea,
            sizeof(linea),
            "SUB:%s\n",
            argv[i]
        );


        send(
            sock,
            linea,
            (int)strlen(linea),
            0
        );


        printf(
            "[subscriber] suscrito al tema '%s'\n",
            argv[i]
        );
    }


    printf(
        "[subscriber] esperando actualizaciones...\n"
    );


    char buf[TAM_BUFER];

    int len = 0;


    while (1) {

        int n =
            recv(
                sock,
                buf + len,
                TAM_BUFER - len,
                0
            );


        if (
            n <= 0
        ) {

            printf(
                "[subscriber] el broker cerro la conexion\n"
            );

            break;
        }


        len += n;


        /* TCP no respeta los limites de cada send() del otro lado:
         * puede llegar mas de una linea junta o una linea cortada
         * a la mitad, entonces toca reconstruir usando '\n'.
         */

        char *inicio =
            buf;

        char *nl;


        while (
            (
                nl = memchr(
                    inicio,
                    '\n',
                    len - (inicio - buf)
                )
            )
            != NULL
        ) {

            *nl = '\0';


            if (
                strncmp(
                    inicio,
                    "MSG:",
                    4
                ) == 0
            ) {

                char *tema =
                    inicio + 4;


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


            inicio =
                nl + 1;
        }


        int restante =
            len - (inicio - buf);


        memmove(
            buf,
            inicio,
            restante
        );


        len =
            restante;
    }


    CERRAR_SOCKET(sock);


#ifdef _WIN32
    WSACleanup();
#endif


    return 0;
}