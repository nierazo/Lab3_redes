/*
 * Broker de publicacion/suscripcion usando UDP.
 *
 * A diferencia de la version TCP, aqui no hay conexiones: el broker
 * recibe datagramas sueltos y, segun su contenido, registra una
 * direccion como suscrita a un tema o reenvia un mensaje a todas
 * las direcciones suscritas a ese tema.
 *
 * Comandos:
 *   SUB:tema          Suscribirse a un tema
 *   MSG:tema:mensaje  Publicar un mensaje
 *
 * Uso:
 *   ./broker_udp <puerto>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "common.h"


/*
 * Guarda la direccion de origen de cada cliente junto con el tema
 * al que se suscribio. Un cliente puede tener varias suscripciones.
 */
typedef struct {
    struct sockaddr_in direccion;
    char tema[TEMA_MAX];
} suscripcion_t;

static suscripcion_t suscripciones[MAX_SUBS];
static int num_suscripciones = 0;


/* Compara dos direcciones IP:puerto para saber si son la misma. */
static int misma_direccion(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr
        && a->sin_port == b->sin_port;
}


/*
 * Agrega una suscripcion a la lista compartida, si no estaba ya
 * registrada (un cliente puede mandar el mismo SUB mas de una vez).
 */
static void agregar_suscripcion(const struct sockaddr_in *direccion, const char *tema) {
    for (int i = 0; i < num_suscripciones; i++) {
        if (misma_direccion(&suscripciones[i].direccion, direccion)
            && strcmp(suscripciones[i].tema, tema) == 0) {
            return;
        }
    }

    if (num_suscripciones >= MAX_SUBS) {
        fprintf(stderr, "[broker] tabla de suscripciones llena\n");
        return;
    }

    suscripcion_t *nueva = &suscripciones[num_suscripciones];

    nueva->direccion = *direccion;
    strncpy(nueva->tema, tema, TEMA_MAX - 1);
    nueva->tema[TEMA_MAX - 1] = '\0';

    num_suscripciones++;

    printf(
        "[broker] %s:%d suscrito al tema '%s'\n",
        inet_ntoa(direccion->sin_addr),
        ntohs(direccion->sin_port),
        tema
    );
}


/* Reenvia un mensaje a todos los clientes suscritos al tema. */
static void reenviar_mensaje(
    int socket_udp,
    const char *tema,
    const char *mensaje,
    int longitud_mensaje
) {
    int num_destinos = 0;

    for (int i = 0; i < num_suscripciones; i++) {
        if (strcmp(suscripciones[i].tema, tema) == 0) {
            sendto(
                socket_udp,
                mensaje,
                longitud_mensaje,
                0,
                (struct sockaddr *)&suscripciones[i].direccion,
                sizeof(suscripciones[i].direccion)
            );

            num_destinos++;
        }
    }

    printf(
        "[broker] tema '%s': mensaje enviado a %d suscriptor(es)\n",
        tema,
        num_destinos
    );
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

    int socket_udp = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_udp < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in direccion_local;
    memset(&direccion_local, 0, sizeof(direccion_local));

    direccion_local.sin_family = AF_INET;
    direccion_local.sin_addr.s_addr = htonl(INADDR_ANY);
    direccion_local.sin_port = htons((unsigned short)puerto);

    if (bind(
            socket_udp,
            (struct sockaddr *)&direccion_local,
            sizeof(direccion_local)
        ) < 0) {

        perror("bind");
        close(socket_udp);
        return 1;
    }

    printf("[broker] escuchando en el puerto %d (UDP)\n", puerto);

    char datagrama[TAM_BUFER];

    while (1) {
        struct sockaddr_in direccion_remitente;
        socklen_t longitud = sizeof(direccion_remitente);

        int bytes_leidos = recvfrom(
            socket_udp,
            datagrama,
            TAM_BUFER - 1,
            0,
            (struct sockaddr *)&direccion_remitente,
            &longitud
        );

        if (bytes_leidos <= 0) {
            continue;
        }

        datagrama[bytes_leidos] = '\0';

        if (strncmp(datagrama, "SUB:", 4) == 0) {
            agregar_suscripcion(&direccion_remitente, datagrama + 4);

        } else if (strncmp(datagrama, "MSG:", 4) == 0) {
            char *inicio_tema = datagrama + 4;
            char *separador = strchr(inicio_tema, ':');

            /*
             * El primer ':' despues de MSG: separa el tema
             * del contenido del mensaje.
             */
            if (separador == NULL) {
                fprintf(stderr, "[broker] mensaje mal formado: %s\n", datagrama);
                continue;
            }

            int longitud_tema = (int)(separador - inicio_tema);

            if (longitud_tema >= TEMA_MAX) {
                longitud_tema = TEMA_MAX - 1;
            }

            char tema[TEMA_MAX];
            memcpy(tema, inicio_tema, longitud_tema);
            tema[longitud_tema] = '\0';

            /*
             * Se reenvia el datagrama tal cual llego, sin tocar
             * el contenido del mensaje.
             */
            reenviar_mensaje(socket_udp, tema, datagrama, bytes_leidos);

        } else {
            fprintf(stderr, "[broker] comando desconocido: %s\n", datagrama);
        }
    }

    close(socket_udp);

    return 0;
}
