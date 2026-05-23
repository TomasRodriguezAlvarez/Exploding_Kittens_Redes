# Fundamentales para que sea apto
1. Procesos separados: un servidor y varios clientes.
2. IPC real: pipes/FIFOs o sockets.
3. Mensajes estructurados: acción, jugador, datos, timestamp Lamport.
4. Relojes de Lamport: cliente y servidor.
5. Log distribuido: mostrar algo como [L=12] Player 2: DRAW_CARD.
6. Turnos controlados por servidor: solo el jugador activo puede actuar.
7. Estado compartido centralizado: mazo, descarte, manos, vivos.
8. Reglas base del juego: robar al final del turno, Exploding Kitten elimina salvo Defuse, gana el último vivo .


# Valor agregado recomendable
1. Sockets TCP en vez de solo pipes, para que parezca más distribuido.
2. Interfaz simple en terminal con menú por jugador.
3. Logs con correlation ID para auditar cada acción, idea vista en seguridad y auditoría .
4. Autenticación básica de jugadores con token o clave simple, inspirado en JWT/API keys .
5. Manejo de fallos: jugador desconectado, acción inválida, mensaje duplicado.
6. ACKs del cliente después de recibir nuevo estado.
7. Modo replay: reconstruir la partida desde el log Lamport.
8. Cartas especiales: Skip, See the Future, Attack, Nope, Favor, pares y combos.

# Decisiones principales

1. Sockets TCP sobre pipes
Usaría sockets TCP porque permiten que varios jugadores se conecten desde distintos terminales o computadores.
Los pipes sirven como IPC local, pero se ven menos “multijugador real”.

2. Arquitectura cliente-servidor sobre peer-to-peer
Usaría un servidor central como coordinador.
Exploding Kittens necesita un estado único: mazo, turnos, manos, descarte y jugadores vivos. Con peer-to-peer habría más riesgo de inconsistencias.

3. Servidor como fuente única de verdad
El cliente no decide si una jugada es válida. Solo pide: “quiero jugar esta carta” o “quiero robar”.
El servidor valida, aplica la regla y luego avisa a todos.

4. Consistencia fuerte sobre eventual
El juego no puede permitir que un jugador vea un mazo distinto o que dos jugadores roben al mismo tiempo.
Por eso conviene consistencia fuerte: todos ven el mismo estado después de cada acción.

5. Lamport sobre reloj físico
No usaría la hora real del computador, porque en sistemas distribuidos no hay reloj global confiable.
Cada acción tendrá timestamp Lamport para ordenar eventos y demostrar causalidad.

6. Turnos como mecanismo de exclusión mutua
Solo el jugador activo puede modificar el estado.
Esto demuestra exclusión mutua de forma natural: un solo proceso entra a la “sección crítica” del juego.

7. Threads en servidor para manejar clientes
El servidor puede tener un hilo por jugador conectado.
Pero la modificación del estado del juego debe protegerse con un mutex o cola de acciones para evitar condiciones de carrera.

8. Broadcast después de cada acción
Cada vez que cambia el estado, el servidor manda actualización a todos.
Así se demuestra coordinación distribuida y consistencia.

# Valor para la presentación

Lo más demostrable sería mostrar:
1. varias terminales abiertas como jugadores;
2. un servidor mostrando logs con Lamport;
3. cada acción con timestamp;
4. validación de turnos;
5. una desconexión o acción inválida;
6. que todos reciben el mismo estado actualizado.


## Pantalla 1: ingreso
- Nombre del jugador.
- Clave de sala.
- Botón Unirse.
- Mensaje de error si la clave está mal o la sala está llena.


## Pantalla 2: sala de espera
- Lista de jugadores conectados.
- Botón Iniciar partida.
- Mensaje: “Esperando mínimo 2 jugadores”.
- ID del jugador propio.

## Pantalla 3: partida
- Tu nombre e ID.
- Turno actual.
- Indicador grande: Es tu turno / Esperando turno.
- Cartas restantes en el mazo.
- Turnos pendientes.
- Lista de jugadores vivos/eliminados.
- Tu mano como botones/cartas visuales.
- Botón Robar carta.
- Botón Actualizar o actualización automática.

## Datos técnicos visibles pero no invasivos
- Lamport.
- Correlation ID o contador de acción.
- Estado de conexión.
- Último mensaje del servidor.

## Acciones del jugador
- Start: iniciar partida.
- Draw: robar.
- Play: jugar carta.
- PlayPair: elegir dos cartas iguales.
- Nope: usable aunque no sea tu turno.
- Players ya no será necesario como comando, porque la lista estará siempre visible.

## Cosas que NO debería ver el cliente
- Mano de otros jugadores.
- Orden completo del mazo.
- Cartas ocultas, salvo con Ver el Futuro.
- Logs completos del servidor.

## Cosas que sí puede ver para demo
- Su propia mano.
- Cantidad de cartas del mazo.
- Jugadores vivos.
- Turno actual.
- Última acción pública.
- Lamport como evidencia técnica.


Orden recomendado ahora
Botones para elegir objetivo por nombre.
Mejorar visual de jugadores alrededor de la mesa.
Mejorar mano de cartas.
Agregar imágenes de cartas.
Animaciones.
Sonidos.

1. Mano en abanico

Base visual más importante.

2. Hover mejorado

Complementa el abanico.

3. Cartas seleccionadas brillando

Ya tenemos selección; solo falta embellecer.

4. Carta jugada al centro

Primera animación “real”.

5. Robo de carta

Complementa la anterior.

6. Nope

Shake rojo rápido.

7. Exploding Kitten

Explosión/flash pantalla.

8. Defuse

Pulso verde calmando explosión.

9. Jugador eliminado

Fade/grayscale.

10. Inicio de partida

Zoom suave de la mesa.