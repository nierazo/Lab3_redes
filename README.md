# Laboratorio 3 — Análisis Capa de Transporte y Sockets

Sistema de noticias deportivas en tiempo real implementado con el patrón
publicación–suscripción, en dos versiones: una sobre sockets **TCP** y otra
sobre sockets **UDP**. Todo está escrito en C usando únicamente la API de
sockets de BSD (`socket`, `bind`, `listen`, `accept`, `connect`, `send`,
`recv`, `sendto`, `recvfrom`, `select`), sin librerías externas.

Además, como **bono**, se incluye una tercera versión sobre **QUIC**
(ver sección al final de este documento).

## Archivos

- `common.h` — constantes compartidas (tamaños de buffer, tema, etc.)
- `broker_tcp.c`, `publisher_tcp.c`, `subscriber_tcp.c` — versión TCP
- `broker_udp.c`, `publisher_udp.c`, `subscriber_udp.c` — versión UDP
- `quic_common.h`, `broker_quic.c`, `publisher_quic.c`, `subscriber_quic.c` — bono, versión QUIC

## Protocolo de aplicación

Mensajes de texto plano, uno por línea:

- `SUB:<tema>` — el suscriptor se registra en un tema (por ejemplo, un partido).
- `MSG:<tema>:<texto>` — el publicador envía un evento de ese tema.

El broker no modifica `<texto>`; solo reenvía cada `MSG` a los clientes que
se hayan suscrito a ese `<tema>`.

## Compilación

```bash
make
```

Genera los seis ejecutables. `make clean` los elimina.

## Ejecución

### Versión TCP

```bash
./broker_tcp 9000
./subscriber_tcp 127.0.0.1 9000 partido1
./subscriber_tcp 127.0.0.1 9000 partido2
./publisher_tcp 127.0.0.1 9000 partido1 10
./publisher_tcp 127.0.0.1 9000 partido2 10
```

### Versión UDP

```bash
./broker_udp 9001
./subscriber_udp 127.0.0.1 9001 partido1
./subscriber_udp 127.0.0.1 9001 partido2
./publisher_udp 127.0.0.1 9001 partido1 10
./publisher_udp 127.0.0.1 9001 partido2 10
```

Un `subscriber` puede suscribirse a varios temas pasándolos como argumentos
adicionales (`./subscriber_tcp 127.0.0.1 9000 partido1 partido2`). El último
argumento de los publicadores (número de mensajes) es opcional; por defecto
envía 10.

## Captura con Wireshark

Antes de ejecutar los publicadores, iniciar la captura en la interfaz de
loopback (o la interfaz real si se prueba entre máquinas distintas) filtrando
por el puerto del broker, por ejemplo `tcp.port == 9000` o `udp.port == 9001`.
Guardar las capturas como `tcp_pubsub.pcap` y `udp_pubsub.pcap` para el
informe.

## Notas de diseño

- El broker TCP usa `select()` para atender múltiples conexiones simultáneas
  en un solo hilo, y arma las líneas de cada cliente en un buffer propio
  porque TCP entrega los datos como un flujo continuo de bytes (un mensaje
  puede llegar partido en varios `recv()`, o varios mensajes pueden llegar
  juntos en uno solo).
- El broker UDP no mantiene conexiones: cada suscriptor queda identificado
  por su dirección IP y puerto de origen, obtenidos del datagrama `SUB` que
  envía. No hay reconexión automática si el broker se cae; el suscriptor
  tendría que volver a enviar su `SUB`.

## Bono: versión QUIC

`broker_quic.c`, `publisher_quic.c` y `subscriber_quic.c` implementan el
mismo sistema de noticias deportivas sobre **QUIC** (RFC 9000), usando el
mismo protocolo de aplicación (`SUB:<tema>` / `MSG:<tema>:<texto>`) que las
versiones TCP y UDP, para que la comparación entre los tres sea justa.

### ¿Por qué con una librería, y cuál?

QUIC no es "UDP con otro nombre": el estándar exige integrar un handshake
completo de **TLS 1.3** dentro del transporte (control de congestión,
recuperación de pérdidas, cifrado por paquete, migración de conexión, IDs
de conexión, etc.). Reimplementar eso desde cero en sockets crudos está
fuera del alcance de un bono de laboratorio, así que se usa una
implementación real de QUIC ya existente. En vez de traer una librería de
terceros (ngtcp2, msquic, lsquic...), se usa la que trae integrada
**OpenSSL 3.5+** en `libssl` (`openssl/quic.h`), que expone un cliente y un
servidor QUIC completos con una interfaz muy similar a los sockets clásicos
(`SSL_new`, `SSL_connect`, `SSL_accept_connection`, `SSL_read_ex`,
`SSL_write_ex`, etc.), y que sigue viajando sobre un socket UDP creado y
administrado por nuestro propio código con `socket()`/`bind()`/`connect()`.
Cada llamada a esa API usada en los tres archivos está comentada en el
código explicando para qué sirve, tal como pide el enunciado para el uso
de librerías.

Requiere **OpenSSL 3.5 o superior** con soporte de QUIC compilado (`openssl
version` y que exista el header `openssl/quic.h`). En macOS con Homebrew:

```bash
brew install openssl@3
```

### Compilación y certificado

QUIC exige TLS, por lo que el broker necesita un certificado propio (en
este laboratorio, autofirmado, ya que no hay una CA real de por medio):

```bash
make certs   # genera cert.pem y key.pem (validos 365 dias)
make quic    # compila broker_quic, publisher_quic y subscriber_quic
```

Estos dos targets son independientes de `make all`: si el equipo no tiene
OpenSSL con QUIC, el entregable base (TCP/UDP) compila y corre igual.

### Ejecución

```bash
./broker_quic 9443
./subscriber_quic 127.0.0.1 9443 partido1
./subscriber_quic 127.0.0.1 9443 partido2
./publisher_quic 127.0.0.1 9443 partido1 10
./publisher_quic 127.0.0.1 9443 partido2 10
```

Para capturar con Wireshark, filtrar por `udp.port == 9443` (todo el
tráfico QUIC va sobre UDP) y, si se quiere ver el contenido del handshake
TLS, usar el disector de QUIC de Wireshark (versiones recientes lo
reconocen automáticamente).

### Diferencias de diseño frente a TCP/UDP

- El broker QUIC atiende cada conexión con un **hilo dedicado**
  (`pthread`) en vez de `select()`: la API bloqueante de QUIC de OpenSSL
  resuelve internamente los reintentos/ACKs propios del protocolo dentro
  de cada llamada bloqueante, y ese modelo combina mejor con un hilo por
  conexión que con multiplexión manual de un solo hilo.
- Un **stream QUIC** (como uno TCP) es un flujo de bytes fiable y
  ordenado, así que el broker y el suscriptor QUIC reutilizan el mismo
  ensamblado de líneas por `\n` que la versión TCP.
- La tabla de suscripciones del broker queda protegida con un mutex
  (`pthread_mutex_t`) porque, a diferencia del broker TCP (un solo hilo),
  aquí varios hilos pueden leer/escribirla al mismo tiempo.
- Cada publicador/suscriptor abre **una conexión QUIC = un stream
  bidireccional por defecto**, análogo a una conexión TCP. QUIC permite
  multiplexar varios streams independientes por conexión (por ejemplo, uno
  por tema) sin que el retraso de un stream bloquee a los demás —una
  ventaja real sobre TCP que vale la pena mencionar en el análisis del
  informe—, pero no se explota aquí para mantener el código comparable en
  tamaño y complejidad con las otras dos versiones.
- El certificado es autofirmado, por lo que cliente y broker deshabilitan
  la verificación de la cadena de confianza (`SSL_VERIFY_NONE`); en un
  sistema real se validaría contra una CA conocida.
