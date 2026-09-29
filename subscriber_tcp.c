/*
 * subscriber_tcp.c
 * Suscriptor (hincha) que se conecta al broker por TCP, se suscribe a
 * uno o varios temas (partidos) y muestra en pantalla las
 * actualizaciones que recibe en vivo.
 *
 * Uso: ./subscriber_tcp <ip_broker> <puerto> <tema1> [tema2 ...]
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

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", server_ip);
        exit(1);
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect"); exit(1);
    }

    for (int i = 3; i < argc; i++) {
        char line[BUFFER_SIZE];
        snprintf(line, sizeof(line), "SUB:%s\n", argv[i]);
        send(sock, line, strlen(line), 0);
        printf("[subscriber] suscrito al tema '%s'\n", argv[i]);
    }
    printf("[subscriber] esperando actualizaciones...\n");

    char inbuf[BUFFER_SIZE];
    int inlen = 0;
    while (1) {
        int n = recv(sock, inbuf + inlen, BUFFER_SIZE - inlen, 0);
        if (n <= 0) {
            printf("[subscriber] el broker cerro la conexion\n");
            break;
        }
        inlen += n;

        /* TCP es un flujo de bytes: puede llegar mas de una linea junta,
         * o una linea partida en varios recv(), por eso se ensambla aqui */
        char *start = inbuf;
        char *nl;
        while ((nl = memchr(start, '\n', inlen - (start - inbuf))) != NULL) {
            *nl = '\0';
            if (strncmp(start, "MSG:", 4) == 0) {
                char *topic = start + 4;
                char *sep = strchr(topic, ':');
                if (sep != NULL) {
                    *sep = '\0';
                    printf(">> [%s] %s\n", topic, sep + 1);
                }
            }
            start = nl + 1;
        }
        int remaining = inlen - (start - inbuf);
        memmove(inbuf, start, remaining);
        inlen = remaining;
    }

    close(sock);
    return 0;
}
