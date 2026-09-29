/*
 * subscriber_udp.c
 * Suscriptor (hincha) que registra su interes en uno o varios temas
 * (partidos) enviando un datagrama SUB por cada uno, y despues queda
 * escuchando los mensajes que el broker le reenvia por UDP.
 *
 * Uso: ./subscriber_udp <ip_broker> <puerto> <tema1> [tema2 ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0); /* salida linea a linea, util para seguir el log */

    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        exit(1);
    }
    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

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

    for (int i = 3; i < argc; i++) {
        char line[BUFFER_SIZE];
        int len = snprintf(line, sizeof(line), "SUB:%s", argv[i]);
        sendto(sock, line, len, 0, (struct sockaddr *)&broker_addr, sizeof(broker_addr));
        printf("[subscriber] suscrito al tema '%s'\n", argv[i]);
    }
    printf("[subscriber] esperando actualizaciones...\n");

    char buf[BUFFER_SIZE];
    while (1) {
        struct sockaddr_in from_addr;
        socklen_t len = sizeof(from_addr);
        int n = recvfrom(sock, buf, BUFFER_SIZE - 1, 0,
                          (struct sockaddr *)&from_addr, &len);
        if (n <= 0) continue;
        buf[n] = '\0';

        if (strncmp(buf, "MSG:", 4) == 0) {
            char *topic = buf + 4;
            char *sep = strchr(topic, ':');
            if (sep != NULL) {
                *sep = '\0';
                printf(">> [%s] %s\n", topic, sep + 1);
            }
        }
    }

    close(sock);
    return 0;
}
