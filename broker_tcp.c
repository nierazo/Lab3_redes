/*
 * broker_tcp.c
 * Broker del sistema de noticias deportivas usando sockets TCP.
 *
 * Acepta conexiones de publicadores y suscriptores por igual (el rol
 * de cada conexion se determina segun el primer tipo de mensaje que
 * envie). Usa select() para atender a todos los clientes en un solo
 * hilo, sin bloquearse esperando a uno solo de ellos.
 *
 * Uso: ./broker_tcp <puerto>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include "common.h"

typedef struct {
    int fd;                     /* -1 si el slot esta libre */
    char inbuf[BUFFER_SIZE];    /* buffer de ensamblado, TCP es un flujo de bytes */
    int inlen;
} client_t;

typedef struct {
    int fd;
    char topic[TOPIC_MAX];
} subscription_t;

static client_t clients[MAX_CLIENTS];
static subscription_t subs[MAX_SUBS];
static int n_subs = 0;

static void add_subscription(int fd, const char *topic) {
    if (n_subs >= MAX_SUBS) {
        fprintf(stderr, "[broker] tabla de suscripciones llena\n");
        return;
    }
    strncpy(subs[n_subs].topic, topic, TOPIC_MAX - 1);
    subs[n_subs].topic[TOPIC_MAX - 1] = '\0';
    subs[n_subs].fd = fd;
    n_subs++;
    printf("[broker] fd=%d suscrito al tema '%s'\n", fd, topic);
}

static void remove_subscriptions_of(int fd) {
    int i = 0;
    while (i < n_subs) {
        if (subs[i].fd == fd) {
            subs[i] = subs[n_subs - 1];
            n_subs--;
        } else {
            i++;
        }
    }
}

static void broadcast_to_topic(const char *topic, const char *line) {
    int sent = 0;
    for (int i = 0; i < n_subs; i++) {
        if (strcmp(subs[i].topic, topic) == 0) {
            if (send(subs[i].fd, line, strlen(line), 0) >= 0) sent++;
        }
    }
    printf("[broker] tema '%s': mensaje reenviado a %d suscriptor(es)\n", topic, sent);
}

/* procesa una linea completa (sin el '\n') recibida de un cliente */
static void process_line(int fd, char *line) {
    if (strncmp(line, "SUB:", 4) == 0) {
        add_subscription(fd, line + 4);
    } else if (strncmp(line, "MSG:", 4) == 0) {
        char *topic = line + 4;
        char *sep = strchr(topic, ':');
        if (sep == NULL) {
            fprintf(stderr, "[broker] mensaje mal formado: %s\n", line);
            return;
        }
        *sep = '\0';
        char out[BUFFER_SIZE];
        snprintf(out, sizeof(out), "MSG:%s:%s\n", topic, sep + 1);
        broadcast_to_topic(topic, out);
    } else {
        fprintf(stderr, "[broker] comando desconocido de fd=%d: %s\n", fd, line);
    }
}

/* saca del buffer del cliente todas las lineas completas que haya y las procesa */
static void handle_client_data(client_t *c) {
    char *start = c->inbuf;
    char *nl;
    while ((nl = memchr(start, '\n', c->inlen - (start - c->inbuf))) != NULL) {
        *nl = '\0';
        process_line(c->fd, start);
        start = nl + 1;
    }
    int remaining = c->inlen - (start - c->inbuf);
    memmove(c->inbuf, start, remaining);
    c->inlen = remaining;
}

static void close_client(int slot) {
    printf("[broker] fd=%d se desconecto\n", clients[slot].fd);
    close(clients[slot].fd);
    remove_subscriptions_of(clients[slot].fd);
    clients[slot].fd = -1;
    clients[slot].inlen = 0;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0); /* salida linea a linea, util para seguir el log */

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(1);
    }
    int port = atoi(argv[1]);

    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listen_fd, 16) < 0) { perror("listen"); exit(1); }

    printf("[broker] escuchando en el puerto %d (TCP)\n", port);

    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        int max_fd = listen_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].fd != -1) {
                FD_SET(clients[i].fd, &read_fds);
                if (clients[i].fd > max_fd) max_fd = clients[i].fd;
            }
        }

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            continue;
        }

        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t len = sizeof(client_addr);
            int new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &len);
            if (new_fd < 0) {
                perror("accept");
            } else {
                int slot = -1;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (clients[i].fd == -1) { slot = i; break; }
                }
                if (slot == -1) {
                    fprintf(stderr, "[broker] limite de clientes alcanzado\n");
                    close(new_fd);
                } else {
                    clients[slot].fd = new_fd;
                    clients[slot].inlen = 0;
                    printf("[broker] nueva conexion fd=%d desde %s:%d\n",
                           new_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int fd = clients[i].fd;
            if (fd == -1 || !FD_ISSET(fd, &read_fds)) continue;

            int space = BUFFER_SIZE - clients[i].inlen;
            if (space <= 0) {
                fprintf(stderr, "[broker] fd=%d supero el buffer de linea, se cierra\n", fd);
                close_client(i);
                continue;
            }

            int n = recv(fd, clients[i].inbuf + clients[i].inlen, space, 0);
            if (n <= 0) {
                close_client(i);
            } else {
                clients[i].inlen += n;
                handle_client_data(&clients[i]);
            }
        }
    }

    return 0;
}
