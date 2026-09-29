/*
 * quic_common.h
 * Constantes propias de la version QUIC del sistema (ademas de las
 * definidas en common.h, que se siguen usando para los tamanos del
 * protocolo de aplicacion: SUB:<tema> / MSG:<tema>:<texto>).
 *
 * QUIC exige TLS 1.3 y negociacion de ALPN (Application-Layer Protocol
 * Negotiation) como parte del handshake; por eso el broker necesita un
 * certificado propio y ambas partes deben coincidir en el identificador
 * de protocolo de aplicacion.
 */

#ifndef QUIC_COMMON_H
#define QUIC_COMMON_H

#define QUIC_ALPN_PROTO "pubsub-quic/1"
#define QUIC_CERT_FILE  "cert.pem"
#define QUIC_KEY_FILE   "key.pem"

#endif
