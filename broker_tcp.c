/*
 * Broker de publicacion/suscripcion usando TCP.
 *
 * Acepta conexiones de publicadores y suscriptores sin distinguir el
 * rol de antemano: el primer comando que mande cada cliente es lo
 * que decide si queda registrado como suscriptor o si esta
 * publicando un evento.
 *
 * Comandos:
 *   SUB:tema          Suscribirse a un tema
 *   MSG:tema:mensaje  Publicar un mensaje
 *
 * Como TCP es orientado a conexion, el broker necesita atender a
 * varios clientes a la vez sin bloquearse esperando a uno solo. Para
 * eso se usa select(), que en cada vuelta revisa cuales sockets
 * tienen datos listos para leer.
 *
 * Uso:
 *   ./broker_tcp <puerto>
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


/*
 * Representa una conexion de cliente aceptada por el broker.
 * El buffer acumula los bytes que van llegando hasta formar una
 * linea completa, porque TCP no garantiza que cada send() del otro
 * lado llegue de un solo golpe en un solo recv().
 */
typedef struct {
    int fd;
    char buffer[TAM_BUFER];
    int bytes_buffer;
} cliente_t;


/*
 * Guarda el descriptor de cada cliente junto con el tema al que
 * se suscribio. Un cliente puede tener varias suscripciones.
 */
typedef struct {
    int fd;
    char tema[TEMA_MAX];
} suscripcion_t;


/* fd == -1 en clientes[] indica que ese espacio esta libre. */
static cliente_t clientes[MAX_CLIENTES];

/* Lista compartida de suscripciones. */
static suscripcion_t suscripciones[MAX_SUBS];
static int num_suscripciones = 0;


/* Agrega una suscripcion a la lista compartida. */
static void agregar_suscripcion(int fd, const char *tema) {
    if (num_suscripciones >= MAX_SUBS) {
        fprintf(stderr, "[broker] tabla de suscripciones llena\n");
        return;
    }

    suscripcion_t *nueva = &suscripciones[num_suscripciones];

    strncpy(nueva->tema, tema, TEMA_MAX - 1);
    nueva->tema[TEMA_MAX - 1] = '\0';
    nueva->fd = fd;

    num_suscripciones++;

    printf("[broker] fd=%d suscrito al tema '%s'\n", fd, tema);
}


/*
 * Elimina todas las suscripciones asociadas a un descriptor.
 * Se reemplaza cada elemento eliminado por el ultimo de la lista,
 * ya que no necesitamos conservar el orden de las suscripciones.
 */
static void quitar_suscripciones(int fd) {
    int i = 0;

    while (i < num_suscripciones) {
        if (suscripciones[i].fd == fd) {
            suscripciones[i] = suscripciones[num_suscripciones - 1];
            num_suscripciones--;
        } else {
            i++;
        }
    }
}


/* Reenvia un mensaje a todos los clientes suscritos al tema. */
static void reenviar_mensaje(const char *tema, const char *mensaje) {
    int num_destinos = 0;

    for (int i = 0; i < num_suscripciones; i++) {
        if (strcmp(suscripciones[i].tema, tema) == 0) {
            if (send(suscripciones[i].fd, mensaje, strlen(mensaje), 0) >= 0) {
                num_destinos++;
            }
        }
    }

    printf(
        "[broker] tema '%s': mensaje enviado a %d suscriptor(es)\n",
        tema,
        num_destinos
    );
}


/* Interpreta y procesa una linea recibida de un cliente. */
static void procesar_mensaje(int fd, char *linea) {
    if (strncmp(linea, "SUB:", 4) == 0) {
        agregar_suscripcion(fd, linea + 4);

    } else if (strncmp(linea, "MSG:", 4) == 0) {
        char *tema = linea + 4;
        char *separador = strchr(tema, ':');

        /*
         * El primer ':' despues de MSG: separa el tema
         * del contenido del mensaje.
         */
        if (separador == NULL) {
            fprintf(stderr, "[broker] mensaje mal formado: %s\n", linea);
            return;
        }

        *separador = '\0';

        char mensaje[TAM_BUFER];

        snprintf(
            mensaje,
            sizeof(mensaje),
            "MSG:%s:%s\n",
            tema,
            separador + 1
        );

        reenviar_mensaje(tema, mensaje);

    } else {
        fprintf(stderr, "[broker] comando desconocido de fd=%d: %s\n", fd, linea);
    }
}


/*
 * Saca del buffer del cliente todas las lineas completas que haya
 * y las procesa. Una sola lectura de recv() no necesariamente trae
 * una linea completa: puede traer varias o dejar parte de una para
 * la siguiente lectura, por eso se busca cada '\n' por separado.
 */
static void procesar_datos_cliente(cliente_t *cliente) {
    char *inicio = cliente->buffer;
    char *salto_linea;

    while ((salto_linea = memchr(
                inicio,
                '\n',
                (size_t)(cliente->buffer + cliente->bytes_buffer - inicio)
            )) != NULL) {

        *salto_linea = '\0';
        procesar_mensaje(cliente->fd, inicio);

        inicio = salto_linea + 1;
    }

    int bytes_restantes = (int)(cliente->buffer + cliente->bytes_buffer - inicio);

    memmove(cliente->buffer, inicio, (size_t)bytes_restantes);
    cliente->bytes_buffer = bytes_restantes;
}


/* Cierra la conexion de un cliente y limpia sus suscripciones. */
static void cerrar_cliente(int indice) {
    printf("[broker] fd=%d se desconecto\n", clientes[indice].fd);

    close(clientes[indice].fd);
    quitar_suscripciones(clientes[indice].fd);

    clientes[indice].fd = -1;
    clientes[indice].bytes_buffer = 0;
}


int main(int argc, char *argv[]) {
    /*
     * Hace que los mensajes de la consola se muestren por linea,
     * algo util para seguir el log mientras el broker esta activo.
     */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return 1;
    }

    int puerto = atoi(argv[1]);

    for (int i = 0; i < MAX_CLIENTES; i++) {
        clientes[i].fd = -1;
    }

    int socket_escucha = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_escucha < 0) {
        perror("socket");
        return 1;
    }

    int valor = 1;
    setsockopt(socket_escucha, SOL_SOCKET, SO_REUSEADDR, &valor, sizeof(valor));

    struct sockaddr_in direccion;
    memset(&direccion, 0, sizeof(direccion));

    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = htonl(INADDR_ANY);
    direccion.sin_port = htons((unsigned short)puerto);

    if (bind(
            socket_escucha,
            (struct sockaddr *)&direccion,
            sizeof(direccion)
        ) < 0) {

        perror("bind");
        close(socket_escucha);
        return 1;
    }

    if (listen(socket_escucha, 16) < 0) {
        perror("listen");
        close(socket_escucha);
        return 1;
    }

    printf("[broker] escuchando en el puerto %d (TCP)\n", puerto);

    while (1) {
        fd_set descriptores_lectura;
        FD_ZERO(&descriptores_lectura);
        FD_SET(socket_escucha, &descriptores_lectura);

        int descriptor_max = socket_escucha;

        for (int i = 0; i < MAX_CLIENTES; i++) {
            if (clientes[i].fd != -1) {
                FD_SET(clientes[i].fd, &descriptores_lectura);

                if (clientes[i].fd > descriptor_max) {
                    descriptor_max = clientes[i].fd;
                }
            }
        }

        if (select(descriptor_max + 1, &descriptores_lectura, NULL, NULL, NULL) < 0) {
            perror("select");
            continue;
        }

        if (FD_ISSET(socket_escucha, &descriptores_lectura)) {
            struct sockaddr_in direccion_cliente;
            socklen_t longitud = sizeof(direccion_cliente);

            int nuevo_fd = accept(
                socket_escucha,
                (struct sockaddr *)&direccion_cliente,
                &longitud
            );

            if (nuevo_fd < 0) {
                perror("accept");
            } else {
                int indice_libre = -1;

                for (int i = 0; i < MAX_CLIENTES; i++) {
                    if (clientes[i].fd == -1) {
                        indice_libre = i;
                        break;
                    }
                }

                if (indice_libre == -1) {
                    fprintf(stderr, "[broker] limite de clientes alcanzado\n");
                    close(nuevo_fd);
                } else {
                    clientes[indice_libre].fd = nuevo_fd;
                    clientes[indice_libre].bytes_buffer = 0;

                    printf(
                        "[broker] nueva conexion fd=%d desde %s:%d\n",
                        nuevo_fd,
                        inet_ntoa(direccion_cliente.sin_addr),
                        ntohs(direccion_cliente.sin_port)
                    );
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTES; i++) {
            int fd = clientes[i].fd;

            if (fd == -1 || !FD_ISSET(fd, &descriptores_lectura)) {
                continue;
            }

            int espacio = TAM_BUFER - clientes[i].bytes_buffer;

            if (espacio <= 0) {
                fprintf(stderr, "[broker] fd=%d supero el buffer de linea, se cierra\n", fd);
                cerrar_cliente(i);
                continue;
            }

            int bytes_leidos = recv(
                fd,
                clientes[i].buffer + clientes[i].bytes_buffer,
                espacio,
                0
            );

            if (bytes_leidos <= 0) {
                cerrar_cliente(i);
            } else {
                clientes[i].bytes_buffer += bytes_leidos;
                procesar_datos_cliente(&clientes[i]);
            }
        }
    }

    close(socket_escucha);

    return 0;
}
