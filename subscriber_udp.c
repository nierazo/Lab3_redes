/*
 * Suscriptor de eventos deportivos usando UDP.
 *
 * Registra su interes en uno o varios partidos mandando un
 * datagrama SUB por cada uno, y se queda escuchando lo que el
 * broker le reenvie.
 *
 * Uso:
 *   ./subscriber_udp <ip_broker> <puerto> <tema1> [tema2 ...]
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

    const char *ip_broker = argv[1];
    int puerto = atoi(argv[2]);

    int socket_udp = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_udp < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in direccion_broker;
    memset(&direccion_broker, 0, sizeof(direccion_broker));

    direccion_broker.sin_family = AF_INET;
    direccion_broker.sin_port = htons((unsigned short)puerto);

    if (inet_pton(AF_INET, ip_broker, &direccion_broker.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", ip_broker);
        close(socket_udp);
        return 1;
    }

    /*
     * Los temas se reciben como argumentos a partir de argv[3], asi
     * que puede haber uno o varios. Se manda un datagrama SUB por
     * cada uno: eso es lo unico que el broker necesita para asociar
     * esta direccion IP:puerto con ese tema en su tabla.
     */
    for (int i = 3; i < argc; i++) {
        char mensaje[TAM_BUFER];
        int longitud = snprintf(mensaje, sizeof(mensaje), "SUB:%s", argv[i]);

        sendto(
            socket_udp,
            mensaje,
            longitud,
            0,
            (struct sockaddr *)&direccion_broker,
            sizeof(direccion_broker)
        );

        printf("[subscriber] suscrito al tema '%s'\n", argv[i]);
    }

    printf("[subscriber] esperando actualizaciones...\n");

    char datagrama[TAM_BUFER];

    /*
     * A diferencia de TCP, aqui no hace falta ensamblar nada: UDP
     * entrega cada datagrama completo o no lo entrega, nunca una
     * mitad. direccion_remitente queda sin usar mas alla de pedirla
     * (recvfrom la exige), porque este programa confia en que todo
     * lo que le llega por este socket viene del broker.
     */
    while (1) {
        struct sockaddr_in direccion_remitente;
        socklen_t longitud = sizeof(direccion_remitente);

        int bytes_leidos = recvfrom(
            socket_udp,
            datagrama,
            TAM_BUFER - 1,
            0,
            (struct sockaddr *)&direccion_remitente,
            &longitud
        );

        if (bytes_leidos <= 0) {
            continue;
        }

        datagrama[bytes_leidos] = '\0';

        if (strncmp(datagrama, "MSG:", 4) == 0) {
            char *tema = datagrama + 4;
            char *separador = strchr(tema, ':');

            if (separador != NULL) {
                *separador = '\0';
                printf(">> [%s] %s\n", tema, separador + 1);
            }
        }
    }

    close(socket_udp);

    return 0;
}
