/* constantes extra para la version QUIC (los tamanos del protocolo
 * siguen saliendo de common.h). QUIC siempre va cifrado con TLS 1.3 y
 * negocia ALPN como parte del handshake, por eso el broker necesita
 * certificado y cliente/servidor tienen que usar el mismo identificador
 * de protocolo a la hora de negociar ALPN */

#ifndef QUIC_COMMON_H
#define QUIC_COMMON_H

#define QUIC_ALPN_PROTO    "pubsub-quic/1"
#define QUIC_ARCHIVO_CERT  "cert.pem"
#define QUIC_ARCHIVO_CLAVE "key.pem"

#endif
