# Exploding Kittens Distribuido

Proyecto desarrollado en C utilizando sockets TCP, servidor multicliente y una interfaz web moderna inspirada en el juego Exploding Kittens.

Incluye:
- servidor concurrente;
- reloj lógico de Lamport;
- interfaz web interactiva;
- sonidos y animaciones;
- reconexión de jugadores;
- cartas con imágenes;
- lógica completa de juego.

# Tecnologías utilizadas

## Backend
- Lenguaje C
- POSIX sockets
- pthreads
- JSON manual

## Frontend
- HTML
- CSS
- JavaScript vanilla

---

# Características implementadas

## Juego
- Crear sala
- Unirse a partida
- Inicio de partida
- Turnos automáticos
- Eliminación de jugadores
- Ganador final

## Cartas soportadas
- Attack
- Skip
- Favor
- Nope
- See the Future
- Defuse
- Exploding Kitten
- Cartas de gato para pares

## Sistema distribuido
- Reloj lógico de Lamport
- Registro de eventos
- Sincronización de acciones
- Servidor concurrente con múltiples clientes

## Interfaz visual
- Mesa estilo tablero
- Mano de cartas tipo abanico
- Cartas reales PNG
- Animaciones
- Efectos visuales
- Sonidos dinámicos
- Indicadores de turno
- Estado de jugadores

## Calidad de vida
- Reconexión automática al recargar
- Selección visual de objetivos
- Botón nueva partida
- Estado final del juego
- Mensajes globales de eventos

# Estructura del proyecto

exploding-kittens/
│
├── web/
│   ├── cards/
│   ├── app.js
│   ├── styles.css
│   └── index.html
│
├── server.c
├── web_server.c
├── game.c
├── game.h
├── protocol.h
├── Makefile
└── README.md

# Compilación

## Linux / WSL

```bash
make clean
make
```

# Ejecución

## Servidor web

```bash
./web_server
```

Luego abrir:

```text
http://localhost:8080
```
o
```text
http://127.0.0.1:8080
```

# Juego en red local

Todos los dispositivos deben estar conectados al mismo Wi-Fi.

Buscar IP local:

```powershell
ipconfig
```

Los demás jugadores pueden ingresar mediante:

```text
http://<IP_LOCAL>:8080
```

# Controles

## Selección de cartas
- Click izquierdo: seleccionar carta
- Click derecho: seleccionar cartas para pares

## Acciones
- Robar carta
- Jugar carta
- Jugar par
- Seleccionar objetivo mediante botones

# Animaciones y sonidos

El juego incluye:
- animación de cartas;
- efecto Nope;
- explosión visual;
- sonidos dinámicos generados con Web Audio API;
- animaciones de hover y selección.

# Flujo general del juego

1. Los jugadores ingresan un nombre.
2. Se unen a la sala.
3. Un jugador inicia la partida.
4. Cada jugador juega cartas o roba.
5. Los jugadores explotan o sobreviven usando Defuse.
6. El último jugador vivo gana.

# Conceptos distribuidos utilizados

## Reloj lógico de Lamport

Cada acción importante:
- incrementa el reloj lógico;
- queda registrada;
- mantiene orden causal de eventos.

## Concurrencia

El servidor utiliza:
- threads;
- mutex;
- múltiples conexiones simultáneas.

# Potenciales mejoras

- Chat en partida
- Música de fondo
- Matchmaking
- Soporte online externo
- Replay visual
- IA/Bots
- Estadísticas de partidas
- Sistema de ranking