# Laboratorio 3 — Análisis Capa de Transporte y Sockets

Sistema de noticias deportivas en tiempo real implementado con el patrón
publicación–suscripción, en dos versiones: una sobre sockets **TCP** y otra
sobre sockets **UDP**. Todo está escrito en C usando únicamente la API de
sockets de BSD (`socket`, `bind`, `listen`, `accept`, `connect`, `send`,
`recv`, `sendto`, `recvfrom`, `select`), sin librerías externas.

## Archivos

- `common.h` — constantes compartidas (tamaños de buffer, tema, etc.)
- `broker_tcp.c`, `publisher_tcp.c`, `subscriber_tcp.c` — versión TCP
- `broker_udp.c`, `publisher_udp.c`, `subscriber_udp.c` — versión UDP

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
