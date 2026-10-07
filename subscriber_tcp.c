/* subscriber (hincha) que se conecta al broker por TCP, se suscribe a
 * uno o varios partidos y va mostrando en pantalla lo que le llega.
 *
 * uso: ./subscriber_tcp <ip_broker> <puerto> <tema1> [tema2 ...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0); // para que los prints salgan de una

    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema1> [tema2 ...]\n", argv[0]);
        exit(1);
    }
    const char *ip_servidor = argv[1];
    int puerto = atoi(argv[2]);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in dir_servidor;
    memset(&dir_servidor, 0, sizeof(dir_servidor));
    dir_servidor.sin_family = AF_INET;
    dir_servidor.sin_port = htons(puerto);
    if (inet_pton(AF_INET, ip_servidor, &dir_servidor.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", ip_servidor);
        exit(1);
    }

    if (connect(sock, (struct sockaddr *)&dir_servidor, sizeof(dir_servidor)) < 0) {
        perror("connect"); exit(1);
    }

    for (int i = 3; i < argc; i++) {
        char linea[TAM_BUFER];
        snprintf(linea, sizeof(linea), "SUB:%s\n", argv[i]);
        send(sock, linea, strlen(linea), 0);
        printf("[subscriber] suscrito al tema '%s'\n", argv[i]);
    }
    printf("[subscriber] esperando actualizaciones...\n");

    char buf[TAM_BUFER];
    int len = 0;
    while (1) {
        int n = recv(sock, buf + len, TAM_BUFER - len, 0);
        if (n <= 0) {
            printf("[subscriber] el broker cerro la conexion\n");
            break;
        }
        len += n;

        // TCP no respeta los limites de cada send() del otro lado: puede
        // llegar mas de una linea junta o una linea cortada a la mitad,
        // entonces toca ensamblar por nuestra cuenta buscando el '\n'
        char *inicio = buf;
        char *nl;
        while ((nl = memchr(inicio, '\n', len - (inicio - buf))) != NULL) {
            *nl = '\0';
            if (strncmp(inicio, "MSG:", 4) == 0) {
                char *tema = inicio + 4;
                char *sep = strchr(tema, ':');
                if (sep != NULL) {
                    *sep = '\0';
                    printf(">> [%s] %s\n", tema, sep + 1);
                }
            }
            inicio = nl + 1;
        }
        int restante = len - (inicio - buf);
        memmove(buf, inicio, restante);
        len = restante;
    }

    close(sock);
    return 0;
}
