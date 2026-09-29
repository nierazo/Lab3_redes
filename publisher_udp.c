/*
 * publisher_udp.c
 * Publicador (periodista deportivo) que envia eventos de un partido
 * al broker mediante datagramas UDP. No hay conexion previa: cada
 * mensaje se envia de forma independiente con sendto().
 *
 * Uso: ./publisher_udp <ip_broker> <puerto> <tema> [num_mensajes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

static const char *events[] = {
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
#define N_EVENTS (int)(sizeof(events) / sizeof(events[0]))

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes]\n", argv[0]);
        exit(1);
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];
    int num_messages = (argc >= 5) ? atoi(argv[4]) : 10;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in broker_addr;
    memset(&broker_addr, 0, sizeof(broker_addr));
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &broker_addr.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", server_ip);
        exit(1);
    }

    printf("[publisher] enviando al broker %s:%d, tema '%s'\n", server_ip, port, topic);

    int minute = 1;
    for (int i = 0; i < num_messages; i++) {
        char text[TEXT_MAX];
        int idx = i % N_EVENTS;
        if (strchr(events[idx], '%') != NULL)
            snprintf(text, sizeof(text), events[idx], minute);
        else
            snprintf(text, sizeof(text), "%s", events[idx]);

        char line[BUFFER_SIZE];
        int len = snprintf(line, sizeof(line), "MSG:%s:%s", topic, text);

        if (sendto(sock, line, len, 0,
                   (struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0) {
            perror("sendto");
            break;
        }
        printf("[publisher] enviado: %s\n", line);

        minute += 2 + (i % 3);
        sleep(1);
    }

    close(sock);
    return 0;
}
