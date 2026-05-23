CC=gcc
CFLAGS=-Wall -Wextra -pthread

all: server client replay web_server

server: server.c game.c
	$(CC) $(CFLAGS) server.c game.c -o server

client: client.c
	$(CC) $(CFLAGS) client.c -o client

replay: replay.c
	$(CC) $(CFLAGS) replay.c -o replay

clean:
	rm -f server client replay
	rm -f server client replay web_server

web_server: web_server.c game.c
	$(CC) $(CFLAGS) web_server.c game.c -o web_server