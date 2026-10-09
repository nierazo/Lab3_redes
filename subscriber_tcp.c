/*
 * Suscriptor de eventos deportivos usando TCP.
 *
 * Se conecta al broker, se suscribe a uno o varios partidos y
 * muestra en pantalla las actualizaciones que le van llegando.
 *
 * Uso:
 *   ./subscriber_tcp <ip_broker> <puerto> <tema1> [tema2 ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "common.h"


int main(int argc, char *argv[]) {
    /*
     * Hace que los mensajes de la consola se muestren por linea,
     * algo util para ver las actualizaciones a medida que llegan.
     */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        return 1;
    }

    const char *ip_servidor = argv[1];
    int puerto = atoi(argv[2]);

    int socket_tcp = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_tcp < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in direccion_servidor;
    memset(&direccion_servidor, 0, sizeof(direccion_servidor));

    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_port = htons((unsigned short)puerto);

    if (inet_pton(AF_INET, ip_servidor, &direccion_servidor.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", ip_servidor);
        close(socket_tcp);
        return 1;
    }

    if (connect(
            socket_tcp,
            (struct sockaddr *)&direccion_servidor,
            sizeof(direccion_servidor)
        ) < 0) {

        perror("connect");
        close(socket_tcp);
        return 1;
    }

    /*
     * Los temas se reciben como argumentos a partir de argv[3], asi
     * que puede haber uno o varios. Se manda un SUB por cada uno
     * sobre la misma conexion: el broker no limita cuantos temas
     * puede seguir un mismo suscriptor.
     */
    for (int i = 3; i < argc; i++) {
        char mensaje[TAM_BUFER];

        snprintf(mensaje, sizeof(mensaje), "SUB:%s\n", argv[i]);
        send(socket_tcp, mensaje, strlen(mensaje), 0);

        printf("[subscriber] suscrito al tema '%s'\n", argv[i]);
    }

    printf("[subscriber] esperando actualizaciones...\n");

    char buffer[TAM_BUFER];
    int bytes_buffer = 0;

    /*
     * El suscriptor se queda indefinidamente leyendo de la conexion:
     * no tiene forma de saber cuantos mensajes le va a mandar el
     * broker, asi que se queda hasta que la conexion se cierre (por
     * ejemplo, si el broker se cae) o el proceso se interrumpa.
     */
    while (1) {
        int bytes_leidos = recv(
            socket_tcp,
            buffer + bytes_buffer,
            TAM_BUFER - bytes_buffer,
            0
        );

        if (bytes_leidos <= 0) {
            printf("[subscriber] el broker cerro la conexion\n");
            break;
        }

        bytes_buffer += bytes_leidos;

        /*
         * TCP no respeta los limites de cada send() del otro lado:
         * puede llegar mas de una linea junta o una linea cortada a
         * la mitad, por eso se ensambla aqui buscando cada '\n'.
         */
        char *inicio = buffer;
        char *salto_linea;

        while ((salto_linea = memchr(
                    inicio,
                    '\n',
                    (size_t)(buffer + bytes_buffer - inicio)
                )) != NULL) {

            *salto_linea = '\0';

            if (strncmp(inicio, "MSG:", 4) == 0) {
                char *tema = inicio + 4;
                char *separador = strchr(tema, ':');

                if (separador != NULL) {
                    *separador = '\0';
                    printf(">> [%s] %s\n", tema, separador + 1);
                }
            }

            inicio = salto_linea + 1;
        }

        int bytes_restantes = (int)(buffer + bytes_buffer - inicio);

        memmove(buffer, inicio, (size_t)bytes_restantes);
        bytes_buffer = bytes_restantes;
    }

    close(socket_tcp);

    return 0;
}
