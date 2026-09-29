/*
 * broker_udp.c
 * Broker del sistema de noticias deportivas usando sockets UDP.
 *
 * A diferencia de la version TCP, aqui no hay conexiones: el broker
 * simplemente recibe datagramas y, segun su contenido, registra una
 * direccion como suscrita a un tema o reenvia un mensaje a todas las
 * direcciones suscritas a ese tema.
 *
 * Uso: ./broker_udp <puerto>
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
    struct sockaddr_in addr;
    char topic[TOPIC_MAX];
} subscription_t;

static subscription_t subs[MAX_SUBS];
static int n_subs = 0;

static int same_addr(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static void add_subscription(const struct sockaddr_in *addr, const char *topic) {
    for (int i = 0; i < n_subs; i++) {
        if (same_addr(&subs[i].addr, addr) && strcmp(subs[i].topic, topic) == 0) {
            return; /* ya estaba suscrita esta direccion a este tema */
        }
    }
    if (n_subs >= MAX_SUBS) {
        fprintf(stderr, "[broker] tabla de suscripciones llena\n");
        return;
    }
    subs[n_subs].addr = *addr;
    strncpy(subs[n_subs].topic, topic, TOPIC_MAX - 1);
    subs[n_subs].topic[TOPIC_MAX - 1] = '\0';
    n_subs++;
    printf("[broker] %s:%d suscrito al tema '%s'\n",
           inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), topic);
}

static void broadcast_to_topic(int sock, const char *topic, const char *msg, int msg_len) {
    int sent = 0;
    for (int i = 0; i < n_subs; i++) {
        if (strcmp(subs[i].topic, topic) == 0) {
            sendto(sock, msg, msg_len, 0,
                   (struct sockaddr *)&subs[i].addr, sizeof(subs[i].addr));
            sent++;
        }
    }
    printf("[broker] tema '%s': mensaje reenviado a %d suscriptor(es)\n", topic, sent);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0); /* salida linea a linea, util para seguir el log */

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(1);
    }
    int port = atoi(argv[1]);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }

    printf("[broker] escuchando en el puerto %d (UDP)\n", port);

    char buf[BUFFER_SIZE];
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int n = recvfrom(sock, buf, BUFFER_SIZE - 1, 0,
                          (struct sockaddr *)&client_addr, &len);
        if (n <= 0) continue;
        buf[n] = '\0';

        if (strncmp(buf, "SUB:", 4) == 0) {
            add_subscription(&client_addr, buf + 4);
        } else if (strncmp(buf, "MSG:", 4) == 0) {
            char *topic_start = buf + 4;
            char *sep = strchr(topic_start, ':');
            if (sep == NULL) {
                fprintf(stderr, "[broker] mensaje mal formado: %s\n", buf);
                continue;
            }
            int topic_len = sep - topic_start;
            if (topic_len >= TOPIC_MAX) topic_len = TOPIC_MAX - 1;
            char topic[TOPIC_MAX];
            memcpy(topic, topic_start, topic_len);
            topic[topic_len] = '\0';

            /* se reenvia el datagrama original tal cual llego, sin tocarlo */
            broadcast_to_topic(sock, topic, buf, n);
        } else {
            fprintf(stderr, "[broker] comando desconocido: %s\n", buf);
        }
    }

    return 0;
}
