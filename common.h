/* constantes que comparten broker, publisher y subscriber.
 *
 * protocolo de aplicacion (texto plano, un mensaje por linea):
 *   SUB:<tema>           -> el subscriber se registra en un tema
 *   MSG:<tema>:<texto>   -> el publisher manda un evento de ese tema
 *
 * el broker no toca <texto>, solo lo reenvia a quien este suscrito */

#ifndef COMMON_H
#define COMMON_H

#define TAM_BUFER    1024  /* tamano maximo de una linea/datagrama */
#define TEMA_MAX     64    /* tamano maximo del nombre de un tema */
#define TEXTO_MAX    400   /* tamano maximo del texto de un evento */
#define MAX_CLIENTES 64    /* conexiones TCP simultaneas soportadas por el broker */
#define MAX_SUBS     256   /* pares (cliente, tema) que el broker puede recordar */

#endif
