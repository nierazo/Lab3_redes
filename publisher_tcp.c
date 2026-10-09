/*
 * Publicador de eventos deportivos usando TCP.
 *
 * Se conecta al broker y envia una serie de eventos de un partido
 * (el tema), simulando a un periodista narrando en vivo.
 *
 * Uso:
 *   ./publisher_tcp <ip_broker> <puerto> <tema> [num_mensajes]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "common.h"


/*
 * Plantillas de eventos que va narrando el publicador, ciclando por
 * el arreglo con el indice del mensaje (i % N_EVENTOS). Los goles
 * llevan un "%d" porque ademas necesitan el minuto del partido; el
 * resto de eventos no lo necesita y se usan tal cual.
 */
static const char *eventos[] = {
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
#define N_EVENTOS (int)(sizeof(eventos) / sizeof(eventos[0]))


int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto> <tema> [num_mensajes]\n", argv[0]);
        return 1;
    }

    const char *ip_servidor = argv[1];
    int puerto = atoi(argv[2]);
    const char *tema = argv[3];
    int num_mensajes = (argc >= 5) ? atoi(argv[4]) : 10;

    int socket_tcp = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_tcp < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in direccion_servidor;
    memset(&direccion_servidor, 0, sizeof(direccion_servidor));

    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_port = htons((unsigned short)puerto);

    if (inet_pton(AF_INET, ip_servidor, &direccion_servidor.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", ip_servidor);
        close(socket_tcp);
        return 1;
    }

    /*
     * A diferencia de UDP, en TCP hay que establecer la conexion
     * antes de poder enviar nada: connect() hace el saludo de tres
     * vias con el broker. Si el broker no esta corriendo o rechaza
     * la conexion, el error aparece aqui y no en cada send().
     */
    if (connect(
            socket_tcp,
            (struct sockaddr *)&direccion_servidor,
            sizeof(direccion_servidor)
        ) < 0) {

        perror("connect");
        close(socket_tcp);
        return 1;
    }

    printf(
        "[publisher] conectado al broker %s:%d, publicando en tema '%s'\n",
        ip_servidor,
        puerto,
        tema
    );

    int minuto = 1;

    /*
     * Se manda un mensaje por vuelta, dejando un sleep(1) entre
     * cada uno para que el partido se vea narrado en tiempo real
     * y para que la captura de Wireshark muestre los paquetes
     * separados en el tiempo en vez de una rafaga instantanea.
     */
    for (int i = 0; i < num_mensajes; i++) {
        char texto[TEXTO_MAX];
        int idx = i % N_EVENTOS;

        if (strchr(eventos[idx], '%') != NULL) {
            snprintf(texto, sizeof(texto), eventos[idx], minuto);
        } else {
            snprintf(texto, sizeof(texto), "%s", eventos[idx]);
        }

        char mensaje[TAM_BUFER];

        snprintf(mensaje, sizeof(mensaje), "MSG:%s:%s\n", tema, texto);

        if (send(socket_tcp, mensaje, strlen(mensaje), 0) < 0) {
            perror("send");
            break;
        }

        printf("[publisher] enviado: %s", mensaje);

        minuto += 2 + (i % 3);
        sleep(1);
    }

    close(socket_tcp);

    return 0;
}
