/* broker del sistema de noticias deportivas, version TCP.
 *
 * no distingue publishers de subscribers: el que manda "SUB:" queda
 * registrado en la tabla, el que manda "MSG:" dispara el reenvio.
 * usa select() para no tener que abrir un hilo por cliente.
 *
 * uso: ./broker_tcp <puerto>
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
    int fd;                    /* -1 si el hueco esta libre */
    char buf[TAM_BUFER];       /* buffer de ensamblado, TCP es un flujo de bytes */
    int len;
} cliente_t;

typedef struct {
    int fd;
    char tema[TEMA_MAX];
} suscripcion_t;

static cliente_t clientes[MAX_CLIENTES];
static suscripcion_t subs[MAX_SUBS];
static int n_subs = 0;

static void agregar_suscripcion(int fd, const char *tema) {
    if (n_subs >= MAX_SUBS) {
        fprintf(stderr, "[broker] tabla de suscripciones llena\n");
        return;
    }
    strncpy(subs[n_subs].tema, tema, TEMA_MAX - 1);
    subs[n_subs].tema[TEMA_MAX - 1] = '\0';
    subs[n_subs].fd = fd;
    n_subs++;
    printf("[broker] fd=%d suscrito al tema '%s'\n", fd, tema);
}

static void quitar_suscripciones_de(int fd) {
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

static void reenviar_a_tema(const char *tema, const char *linea) {
    int enviados = 0;
    for (int i = 0; i < n_subs; i++) {
        if (strcmp(subs[i].tema, tema) == 0) {
            if (send(subs[i].fd, linea, strlen(linea), 0) >= 0) enviados++;
        }
    }
    printf("[broker] tema '%s': mensaje reenviado a %d suscriptor(es)\n", tema, enviados);
}

// procesa una linea ya completa (sin el '\n') que llego de un cliente
static void procesar_linea(int fd, char *linea) {
    if (strncmp(linea, "SUB:", 4) == 0) {
        agregar_suscripcion(fd, linea + 4);
    } else if (strncmp(linea, "MSG:", 4) == 0) {
        char *tema = linea + 4;
        char *sep = strchr(tema, ':');
        if (sep == NULL) {
            fprintf(stderr, "[broker] mensaje mal formado: %s\n", linea);
            return;
        }
        *sep = '\0';
        char salida[TAM_BUFER];
        snprintf(salida, sizeof(salida), "MSG:%s:%s\n", tema, sep + 1);
        reenviar_a_tema(tema, salida);
    } else {
        fprintf(stderr, "[broker] comando desconocido de fd=%d: %s\n", fd, linea);
    }
}

// saca del buffer del cliente las lineas completas que haya y las procesa
static void procesar_datos_cliente(cliente_t *c) {
    char *inicio = c->buf;
    char *nl;
    while ((nl = memchr(inicio, '\n', c->len - (inicio - c->buf))) != NULL) {
        *nl = '\0';
        procesar_linea(c->fd, inicio);
        inicio = nl + 1;
    }
    int restante = c->len - (inicio - c->buf);
    memmove(c->buf, inicio, restante);
    c->len = restante;
}

static void cerrar_cliente(int hueco) {
    printf("[broker] fd=%d se desconecto\n", clientes[hueco].fd);
    close(clientes[hueco].fd);
    quitar_suscripciones_de(clientes[hueco].fd);
    clientes[hueco].fd = -1;
    clientes[hueco].len = 0;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0); // sin esto el log se queda pegado en el buffer

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(1);
    }
    int puerto = atoi(argv[1]);

    for (int i = 0; i < MAX_CLIENTES; i++) clientes[i].fd = -1;

    int fd_escucha = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_escucha < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(fd_escucha, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in dir_servidor;
    memset(&dir_servidor, 0, sizeof(dir_servidor));
    dir_servidor.sin_family = AF_INET;
    dir_servidor.sin_addr.s_addr = INADDR_ANY;
    dir_servidor.sin_port = htons(puerto);

    if (bind(fd_escucha, (struct sockaddr *)&dir_servidor, sizeof(dir_servidor)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(fd_escucha, 16) < 0) { perror("listen"); exit(1); }

    printf("[broker] escuchando en el puerto %d (TCP)\n", puerto);

    while (1) {
        fd_set fds_lectura;
        FD_ZERO(&fds_lectura);
        FD_SET(fd_escucha, &fds_lectura);
        int fd_max = fd_escucha;

        for (int i = 0; i < MAX_CLIENTES; i++) {
            if (clientes[i].fd != -1) {
                FD_SET(clientes[i].fd, &fds_lectura);
                if (clientes[i].fd > fd_max) fd_max = clientes[i].fd;
            }
        }

        if (select(fd_max + 1, &fds_lectura, NULL, NULL, NULL) < 0) {
            perror("select");
            continue;
        }

        if (FD_ISSET(fd_escucha, &fds_lectura)) {
            struct sockaddr_in dir_cliente;
            socklen_t len = sizeof(dir_cliente);
            int nuevo_fd = accept(fd_escucha, (struct sockaddr *)&dir_cliente, &len);
            if (nuevo_fd < 0) {
                perror("accept");
            } else {
                int hueco = -1;
                for (int i = 0; i < MAX_CLIENTES; i++) {
                    if (clientes[i].fd == -1) { hueco = i; break; }
                }
                if (hueco == -1) {
                    fprintf(stderr, "[broker] limite de clientes alcanzado\n");
                    close(nuevo_fd);
                } else {
                    clientes[hueco].fd = nuevo_fd;
                    clientes[hueco].len = 0;
                    printf("[broker] nueva conexion fd=%d desde %s:%d\n",
                           nuevo_fd, inet_ntoa(dir_cliente.sin_addr), ntohs(dir_cliente.sin_port));
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTES; i++) {
            int fd = clientes[i].fd;
            if (fd == -1 || !FD_ISSET(fd, &fds_lectura)) continue;

            int espacio = TAM_BUFER - clientes[i].len;
            if (espacio <= 0) {
                fprintf(stderr, "[broker] fd=%d supero el buffer de linea, se cierra\n", fd);
                cerrar_cliente(i);
                continue;
            }

            int n = recv(fd, clientes[i].buf + clientes[i].len, espacio, 0);
            if (n <= 0) {
                cerrar_cliente(i);
            } else {
                clientes[i].len += n;
                procesar_datos_cliente(&clientes[i]);
            }
        }
    }

    return 0;
}
