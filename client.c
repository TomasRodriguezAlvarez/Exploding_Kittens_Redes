#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#include "protocol.h"
#define LOCAL_HAND_MAX 64

int socket_fd;
int client_lamport_clock = 0;
int my_id = -1;
int next_correlation_id = 1;

typedef struct {
    int id;
    char name[64];
} LocalCard;

LocalCard local_hand[LOCAL_HAND_MAX];
int local_hand_count = 0;

int card_needs_target(int card_id) {
    for (int i = 0; i < local_hand_count; i++) {
        if (local_hand[i].id == card_id) {
            return strstr(local_hand[i].name, "Favor") != NULL;
        }
    }

    return 0;
}

void print_menu() {
    printf("\n====================================\n");
    printf("Comandos disponibles:\n");
    printf("  start  -> iniciar partida\n");
    printf("  draw   -> robar carta\n");
    printf("  play   -> jugar una carta\n");
    printf("  playpair -> jugar par de cartas normales\n");
    printf("  players -> ver jugadores\n");
    printf("  help    -> ver ayuda de comandos\n");
    printf("  exit   -> salir\n");
    printf("====================================\n");
    printf("> ");
    fflush(stdout);
}

void send_message_to_server(Message *msg) {
    client_lamport_clock++;
    msg->lamport_timestamp = client_lamport_clock;
    msg->player_id = my_id;
    msg->correlation_id = next_correlation_id++;

    send(socket_fd, msg, sizeof(Message), 0);
}

void *receive_thread(void *arg) {
    (void)arg;

    Message msg;

    while (recv(socket_fd, &msg, sizeof(Message), 0) > 0) {
        client_lamport_clock =
            (client_lamport_clock > msg.lamport_timestamp
                ? client_lamport_clock
                : msg.lamport_timestamp) + 1;

        if (msg.type == MSG_GAME_STATE) {
            my_id = msg.player_id;

            printf("\n\n========== ESTADO DEL JUEGO ==========\n");
            printf("%s", msg.text);
            printf("======================================\n");

            local_hand_count = 0;

            char *line = strtok(msg.text, "\n");

            while (line != NULL) {
                int id;
                char card_name[64];

                if (sscanf(line, "  ID %d: %63[^\n]", &id, card_name) == 2) {
                    local_hand[local_hand_count].id = id;
                    snprintf(local_hand[local_hand_count].name, 64, "%s", card_name);
                    local_hand_count++;
                }

                line = strtok(NULL, "\n");
            }

            Message ack;
            memset(&ack, 0, sizeof(Message));
            ack.type = MSG_ACK;
            ack.correlation_id = msg.correlation_id;
            send_message_to_server(&ack);
        } else if (msg.type == MSG_INFO) {
            printf("\n[INFO] %s\n", msg.text);
        } else if (msg.type == MSG_ERROR) {
            printf("\n[ERROR] %s\n", msg.text);
        }

        //print_menu();
    }

    printf("\nConexión cerrada por el servidor.\n");
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Uso: %s <IP_SERVIDOR> <PUERTO>\n", argv[0]);
        return 1;
    }

    char *server_ip = argv[1];
    int port = atoi(argv[2]);

    socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }

    if (connect(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        return 1;
    }

    char name[MAX_NAME];

    printf("Ingresa tu nombre: ");
    fgets(name, MAX_NAME, stdin);
    name[strcspn(name, "\n")] = 0;
    char password[MAX_NAME];

    printf("Clave de sala: ");
    fgets(password, MAX_NAME, stdin);
    password[strcspn(password, "\n")] = 0;

    Message join_msg;
    memset(&join_msg, 0, sizeof(Message));
    join_msg.type = MSG_JOIN;
    snprintf(join_msg.text, MAX_TEXT, "%s|%s", name, password);
    send_message_to_server(&join_msg);

    pthread_t tid;
    pthread_create(&tid, NULL, receive_thread, NULL);

    char command[64];

    print_menu();

    while (1) {
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0;

        Message msg;
        memset(&msg, 0, sizeof(Message));

        if (strcmp(command, "start") == 0) {
            msg.type = MSG_START_GAME;
            send_message_to_server(&msg);

        } else if (strcmp(command, "draw") == 0) {
            msg.type = MSG_DRAW_CARD;
            send_message_to_server(&msg);

        } else if (strcmp(command, "play") == 0) {
            msg.type = MSG_PLAY_CARD;

            printf("ID de carta a jugar: ");
            scanf("%d", &msg.card_id);
            getchar();

            if (card_needs_target(msg.card_id)) {
                printf("Jugador objetivo: ");
                scanf("%d", &msg.target_player);
                getchar();
            } else {
                msg.target_player = -1;
            }

            send_message_to_server(&msg);

        } else if (strcmp(command, "playpair") == 0) {
            msg.type = MSG_PLAY_PAIR;

            printf("ID de primera carta: ");
            scanf("%d", &msg.card_id);
            getchar();

            printf("ID de segunda carta: ");
            scanf("%d", &msg.second_card_id);
            getchar();

            printf("Jugador objetivo: ");
            scanf("%d", &msg.target_player);
            getchar();

            send_message_to_server(&msg);

        } else if (strcmp(command, "players") == 0) {
            msg.type = MSG_PLAYERS;
            send_message_to_server(&msg);

        } else if (strcmp(command, "help") == 0) {
            msg.type = MSG_HELP;
            send_message_to_server(&msg);

        } else if (strcmp(command, "exit") == 0) {
            msg.type = MSG_EXIT;
            send_message_to_server(&msg);
            break;

        } else if (strlen(command) > 0) {
            printf("Comando no válido.\n");
            print_menu();
        }
    }
    close(socket_fd);
    return 0;
}