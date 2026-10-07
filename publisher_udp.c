/* publisher que manda eventos de un partido al broker por UDP. no hay
 * conexion previa, cada mensaje sale independiente con sendto().
 *
 * uso: ./publisher_udp <ip_broker> <puerto> <tema> [num_mensajes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
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
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes]\n", argv[0]);
        exit(1);
    }
    const char *ip_broker = argv[1];
    int puerto = atoi(argv[2]);
    const char *tema = argv[3];
    int num_mensajes = (argc >= 5) ? atoi(argv[4]) : 10;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in dir_broker;
    memset(&dir_broker, 0, sizeof(dir_broker));
    dir_broker.sin_family = AF_INET;
    dir_broker.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip_broker, &dir_broker.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", ip_broker);
        exit(1);
    }

    printf("[publisher] enviando al broker %s:%d, tema '%s'\n", ip_broker, puerto, tema);

    int minuto = 1;
    for (int i = 0; i < num_mensajes; i++) {
        char texto[TEXTO_MAX];
        int idx = i % N_EVENTOS;
        if (strchr(eventos[idx], '%') != NULL)
            snprintf(texto, sizeof(texto), eventos[idx], minuto);
        else
            snprintf(texto, sizeof(texto), "%s", eventos[idx]);

        char linea[TAM_BUFER];
        int len = snprintf(linea, sizeof(linea), "MSG:%s:%s", tema, texto);

        if (sendto(sock, linea, len, 0,
                   (struct sockaddr *)&dir_broker, sizeof(dir_broker)) < 0) {
            perror("sendto");
            break;
        }
        printf("[publisher] enviado: %s\n", linea);

        minuto += 2 + (i % 3);
        sleep(1);
    }

    close(sock);
    return 0;
}
