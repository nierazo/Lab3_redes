/* constantes extra para la version QUIC (los tamanos del protocolo
 * siguen saliendo de common.h). QUIC obliga a hacer handshake TLS 1.3
 * con negociacion de ALPN, asi que el broker necesita certificado y
 * cliente/servidor deben coincidir en el identificador de protocolo */

#ifndef QUIC_COMMON_H
#define QUIC_COMMON_H

#define QUIC_ALPN_PROTO    "pubsub-quic/1"
#define QUIC_ARCHIVO_CERT  "cert.pem"
#define QUIC_ARCHIVO_CLAVE "key.pem"

#endif
