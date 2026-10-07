/* subscriber que registra su interes en uno o varios partidos mandando
 * un datagrama SUB por cada uno, y se queda escuchando lo que el
 * broker le reenvie por UDP.
 *
 * uso: ./subscriber_udp <ip_broker> <puerto> <tema1> [tema2 ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0); // asi los prints no se quedan en el buffer

    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        exit(1);
    }
    const char *ip_broker = argv[1];
    int puerto = atoi(argv[2]);

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

    for (int i = 3; i < argc; i++) {
        char linea[TAM_BUFER];
        int len = snprintf(linea, sizeof(linea), "SUB:%s", argv[i]);
        sendto(sock, linea, len, 0, (struct sockaddr *)&dir_broker, sizeof(dir_broker));
        printf("[subscriber] suscrito al tema '%s'\n", argv[i]);
    }
    printf("[subscriber] esperando actualizaciones...\n");

    char datagrama[TAM_BUFER];
    while (1) {
        struct sockaddr_in dir_remitente;
        socklen_t len = sizeof(dir_remitente);
        int n = recvfrom(sock, datagrama, TAM_BUFER - 1, 0,
                          (struct sockaddr *)&dir_remitente, &len);
        if (n <= 0) continue;
        datagrama[n] = '\0';

        if (strncmp(datagrama, "MSG:", 4) == 0) {
            char *tema = datagrama + 4;
            char *sep = strchr(tema, ':');
            if (sep != NULL) {
                *sep = '\0';
                printf(">> [%s] %s\n", tema, sep + 1);
            }
        }
    }

    close(sock);
    return 0;
}
