/* publisher que manda eventos de un partido al broker por UDP. no hay
 * conexion previa, cada mensaje sale independiente con sendto().
 *
 * uso: ./publisher_udp <ip_broker> <puerto> <tema> [num_mensajes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* compatibilidad Windows / Linux */
#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

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


int main(int argc, char *argv[]) {

    if (argc < 4) {

        fprintf(
            stderr,
            "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes]\n",
            argv[0]
        );

        exit(1);
    }


    const char *ip_broker =
        argv[1];

    int puerto =
        atoi(argv[2]);

    const char *tema =
        argv[3];

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
            "[publisher] error creando socket UDP: %d\n",
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


    printf(
        "[publisher] enviando al broker %s:%d, tema '%s'\n",
        ip_broker,
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
            ) != NULL
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
                "MSG:%s:%s",
                tema,
                texto
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
                "[publisher] fallo sendto: %d\n",
                WSAGetLastError()
            );

            break;
        }

#else

        if (
            resultado < 0
        ) {

            perror("sendto");

            break;
        }

#endif


        printf(
            "[publisher] enviado: %s\n",
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


    CERRAR_SOCKET(sock);


#ifdef _WIN32
    WSACleanup();
#endif


    return 0;
}