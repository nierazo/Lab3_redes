/* broker version UDP: aqui no hay conexiones, solo datagramas sueltos.
 * segun lo que traiga cada uno, se registra una direccion como
 * suscrita a un tema o se reenvia un mensaje a todas las direcciones
 * suscritas a ese tema.
 *
 * uso: ./broker_udp <puerto>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"

typedef struct {
    struct sockaddr_in dir;
    char tema[TEMA_MAX];
} suscripcion_t;

static suscripcion_t subs[MAX_SUBS];
static int n_subs = 0;

static int misma_direccion(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void agregar_suscripcion(const struct sockaddr_in *dir, const char *tema) {
    for (int i = 0; i < n_subs; i++) {
        if (misma_direccion(&subs[i].dir, dir) && strcmp(subs[i].tema, tema) == 0) {
            return; // ya estaba suscrita esta direccion a este tema, nada que hacer
        }
    }
    if (n_subs >= MAX_SUBS) {
        fprintf(stderr, "[broker] tabla de suscripciones llena\n");
        return;
    }
    subs[n_subs].dir = *dir;
    strncpy(subs[n_subs].tema, tema, TEMA_MAX - 1);
    subs[n_subs].tema[TEMA_MAX - 1] = '\0';
    n_subs++;
    printf("[broker] %s:%d suscrito al tema '%s'\n",
           inet_ntoa(dir->sin_addr), ntohs(dir->sin_port), tema);
}

static void reenviar_a_tema(int sock, const char *tema, const char *mensaje, int len_mensaje) {
    int enviados = 0;
    for (int i = 0; i < n_subs; i++) {
        if (strcmp(subs[i].tema, tema) == 0) {
            sendto(sock, mensaje, len_mensaje, 0,
                   (struct sockaddr *)&subs[i].dir, sizeof(subs[i].dir));
            enviados++;
        }
    }
    printf("[broker] tema '%s': mensaje reenviado a %d suscriptor(es)\n", tema, enviados);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0); // log sin buffering, para verlo en vivo

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(1);
    }
    int puerto = atoi(argv[1]);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in dir_local;
    memset(&dir_local, 0, sizeof(dir_local));
    dir_local.sin_family = AF_INET;
    dir_local.sin_addr.s_addr = INADDR_ANY;
    dir_local.sin_port = htons(puerto);

    if (bind(sock, (struct sockaddr *)&dir_local, sizeof(dir_local)) < 0) {
        perror("bind"); exit(1);
    }

    printf("[broker] escuchando en el puerto %d (UDP)\n", puerto);

    char datagrama[TAM_BUFER];
    while (1) {
        struct sockaddr_in dir_remitente;
        socklen_t len = sizeof(dir_remitente);
        int n = recvfrom(sock, datagrama, TAM_BUFER - 1, 0,
                          (struct sockaddr *)&dir_remitente, &len);
        if (n <= 0) continue;
        datagrama[n] = '\0';

        if (strncmp(datagrama, "SUB:", 4) == 0) {
            agregar_suscripcion(&dir_remitente, datagrama + 4);
        } else if (strncmp(datagrama, "MSG:", 4) == 0) {
            char *inicio_tema = datagrama + 4;
            char *sep = strchr(inicio_tema, ':');
            if (sep == NULL) {
                fprintf(stderr, "[broker] mensaje mal formado: %s\n", datagrama);
                continue;
            }
            int len_tema = sep - inicio_tema;
            if (len_tema >= TEMA_MAX) len_tema = TEMA_MAX - 1;
            char tema[TEMA_MAX];
            memcpy(tema, inicio_tema, len_tema);
            tema[len_tema] = '\0';

            // se reenvia el datagrama tal cual llego, no se toca el contenido
            reenviar_a_tema(sock, tema, datagrama, n);
        } else {
            fprintf(stderr, "[broker] comando desconocido: %s\n", datagrama);
        }
    }

    return 0;
}
