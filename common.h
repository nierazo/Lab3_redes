/*
 * common.h
 * Constantes compartidas por el broker, el publicador y el suscriptor,
 * tanto en la version TCP como en la version UDP.
 *
 * Protocolo de aplicacion (texto plano, un mensaje por linea):
 *   SUB:<tema>            -> el suscriptor se registra en un tema
 *   MSG:<tema>:<texto>    -> el publicador envia un evento de ese tema
 *
 * El broker no interpreta ni modifica <texto>, solo lo reenvia a los
 * clientes que se hayan suscrito a <tema>.
 */

#ifndef COMMON_H
#define COMMON_H

#define BUFFER_SIZE 1024   /* tamano maximo de una linea/datagrama */
#define TOPIC_MAX   64     /* tamano maximo del nombre de un tema */
#define TEXT_MAX    400    /* tamano maximo del texto de un evento */
#define MAX_CLIENTS 64     /* conexiones TCP simultaneas soportadas por el broker */
#define MAX_SUBS    256    /* pares (cliente, tema) que el broker puede recordar */

#endif
