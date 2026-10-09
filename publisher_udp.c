/*
 * Publicador de eventos deportivos usando UDP.
 *
 * Envia eventos de un partido al broker mediante datagramas sueltos.
 * No hay conexion previa: cada mensaje sale de forma independiente
 * con sendto().
 *
 * Uso:
 *   ./publisher_udp <ip_broker> <puerto> <tema> [num_mensajes]
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

    const char *ip_broker = argv[1];
    int puerto = atoi(argv[2]);
    const char *tema = argv[3];
    int num_mensajes = (argc >= 5) ? atoi(argv[4]) : 10;

    int socket_udp = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_udp < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in direccion_broker;
    memset(&direccion_broker, 0, sizeof(direccion_broker));

    direccion_broker.sin_family = AF_INET;
    direccion_broker.sin_port = htons((unsigned short)puerto);

    if (inet_pton(AF_INET, ip_broker, &direccion_broker.sin_addr) <= 0) {
        fprintf(stderr, "Direccion IP invalida: %s\n", ip_broker);
        close(socket_udp);
        return 1;
    }

    printf(
        "[publisher] enviando al broker %s:%d, tema '%s'\n",
        ip_broker,
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
        int longitud = snprintf(mensaje, sizeof(mensaje), "MSG:%s:%s", tema, texto);

        /*
         * Como no hay connect() previo, cada sendto() lleva su
         * propia direccion de destino. Un valor negativo aqui es
         * un error local (por ejemplo, la interfaz de red cayo);
         * no significa que el datagrama haya llegado al broker,
         * porque UDP no confirma la entrega.
         */
        int enviado = sendto(
            socket_udp,
            mensaje,
            longitud,
            0,
            (struct sockaddr *)&direccion_broker,
            sizeof(direccion_broker)
        );

        if (enviado < 0) {
            perror("sendto");
            break;
        }

        printf("[publisher] enviado: %s\n", mensaje);

        minuto += 2 + (i % 3);
        sleep(1);
    }

    close(socket_udp);

    return 0;
}
