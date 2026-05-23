#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <time.h>

#include "game.h"

GameState game;
pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE *log_file = NULL;

void write_log(int lamport, int cid, int player_id, const char *action);

const char *message_type_to_string(MessageType type) {
    switch (type) {
        case MSG_JOIN: return "JOIN";
        case MSG_START_GAME: return "START_GAME";
        case MSG_PLAY_CARD: return "PLAY_CARD";
        case MSG_PLAY_PAIR: return "PLAY_PAIR";
        case MSG_DRAW_CARD: return "DRAW_CARD";
        case MSG_GAME_STATE: return "GAME_STATE";
        case MSG_ERROR: return "ERROR";
        case MSG_INFO: return "INFO";
        case MSG_PLAYERS: return "PLAYERS";
        case MSG_HELP: return "HELP";
        case MSG_EXIT: return "EXIT";
        case MSG_ACK: return "ACK";
        default: return "UNKNOWN";
    }
}

void send_message(int socket_fd, Message *msg) {
    send(socket_fd, msg, sizeof(Message), 0);
}

void send_info(int socket_fd, const char *text) {
    Message msg;
    memset(&msg, 0, sizeof(Message));
    msg.type = MSG_INFO;
    snprintf(msg.text, MAX_TEXT, "%s", text);
    send_message(socket_fd, &msg);
}

void broadcast_info(const char *text) {
    Message msg;
    memset(&msg, 0, sizeof(Message));
    msg.type = MSG_INFO;
    snprintf(msg.text, MAX_TEXT, "%s", text);

    for (int i = 0; i < game.player_count; i++) {
        if (game.players[i].alive || !game.game_started) {
            send_message(game.players[i].socket_fd, &msg);
        }
    }
}

void send_private_state(int player_index) {
    Player *p = &game.players[player_index];

    Message msg;
    memset(&msg, 0, sizeof(Message));
    msg.type = MSG_GAME_STATE;
    msg.player_id = p->id;
    msg.lamport_timestamp = game.server_lamport_clock;

    char buffer[MAX_TEXT] = "";
    char line[80];

    snprintf(buffer, sizeof(buffer),
        "\n[L=%d]\n"
        "Tu ID: %d\n"
        "Turno actual: Jugador %d\n"
        "Cartas restantes en mazo: %d\n"
        "Turnos pendientes del jugador actual: %d\n"
        "%s\n",
        game.server_lamport_clock,
        p->id,
        game.players[game.current_turn].id,
        game.deck_count, 
        game.pending_turns,
        (player_index == game.current_turn && game.game_started)
            ? ">>> ES TU TURNO <<<"
            : "Esperando turno..."
    );

    strncat(buffer, "\nJugadores:\n", sizeof(buffer) - strlen(buffer) - 1);

    for (int i = 0; i < game.player_count; i++) {
        snprintf(line, sizeof(line), "  Jugador %d: %s | %s\n",
                game.players[i].id,
                game.players[i].name,
                game.players[i].alive ? "Vivo" : "Eliminado");

        strncat(buffer, line, sizeof(buffer) - strlen(buffer) - 1);
    }

    strncat(buffer, "\nTu mano:\n", sizeof(buffer) - strlen(buffer) - 1);

    for (int i = 0; i < p->hand_count; i++) {
        snprintf(line, sizeof(line), "  ID %d: %s\n",
                 p->hand[i].id,
                 card_to_string(p->hand[i].type));
        strncat(buffer, line, sizeof(buffer) - strlen(buffer) - 1);
    }

    snprintf(msg.text, MAX_TEXT, "%s", buffer);
    send_message(p->socket_fd, &msg);
}

void broadcast_states() {
    for (int i = 0; i < game.player_count; i++) {
        send_private_state(i);
    }
}

void finish_game_if_needed() {
    if (game.game_started && alive_players(&game) <= 1) {
        int winner = get_winner(&game);

        if (winner != -1) {
            char text[MAX_TEXT];
            snprintf(text, MAX_TEXT,
                     "¡Jugador %d (%s) gana la partida!",
                     game.players[winner].id,
                     game.players[winner].name);
            broadcast_info(text);
        }

        game.game_started = 0;
    }
}

void handle_draw(int player_index) {
    Player *p = &game.players[player_index];

    if (!p->alive) {
        send_info(p->socket_fd, "Estás eliminado. No puedes realizar acciones.");
        return;
    }

    if (!game.game_started) {
        send_info(p->socket_fd, "La partida todavía no ha comenzado.");
        return;
    }

    if (player_index != game.current_turn) {
        send_info(p->socket_fd, "No es tu turno.");
        return;
    }

    game.has_last_action = 0;
    game.has_stolen_card = 0;

    if (game.deck_count <= 0) {
        send_info(p->socket_fd, "El mazo está vacío. No se puede robar.");
        return;
    }

    Card card = draw_card(&game);
    char log_action[MAX_TEXT];
    snprintf(log_action, MAX_TEXT,
            "DRAW_CARD carta=%s carta_id=%d",
            card_to_string(card.type),
            card.id);

    write_log(game.server_lamport_clock,
            0,
            p->id,
            log_action);

    if (card.id == -1) {
        send_info(p->socket_fd, "Error al robar carta.");
        return;
    }

    char text[MAX_TEXT];

    if (card.type == CARD_EXPLODING) {
        Card removed;
        int has_defuse = 0;

        for (int i = 0; i < p->hand_count; i++) {
            if (p->hand[i].type == CARD_DEFUSE) {
                has_defuse = remove_card_from_hand(p, p->hand[i].id, &removed);
                break;
            }
        }

        if (has_defuse) {
            game.discard[game.discard_count++] = removed;
            game.deck[game.deck_count++] = card;
            shuffle_deck(&game);

            snprintf(text, MAX_TEXT,
                "Jugador %d robó un Gato Explosivo, pero usó Desactivar. El gato vuelve al mazo.",
                p->id);
            broadcast_info(text);
            game.pending_turns--;

            if (game.pending_turns <= 0) {
                game.pending_turns = 1;
                next_turn(&game);
            }
        } else {
            p->alive = 0;
            game.out_of_game[game.out_count++] = card;

            snprintf(text, MAX_TEXT,
                "Jugador %d robó un Gato Explosivo y quedó eliminado.",
                p->id);
            broadcast_info(text);

            finish_game_if_needed();

            if (game.game_started) {
                game.pending_turns--;

                if (game.pending_turns <= 0) {
                    game.pending_turns = 1;
                    next_turn(&game);
                }
            }
        }
    } else {
        add_card_to_hand(p, card);

        snprintf(text, MAX_TEXT,
            "Jugador %d robó una carta. Turnos restantes después de esta acción: %d.",
            p->id,
            game.pending_turns - 1);
        broadcast_info(text);

        game.pending_turns--;

        if (game.pending_turns <= 0) {
            game.pending_turns = 1;
            next_turn(&game);
        }
    }

    if (!validate_card_consistency(&game)) {
        broadcast_info("ERROR DE CONSISTENCIA: una carta aparece duplicada.");
    }

    finish_game_if_needed();
    broadcast_states();
}

void save_last_action(Card played, int player_index, int target_index) {
    game.last_action_card = played;
    game.has_last_action = 1;
    game.last_action_player = player_index;
    game.last_target_player = target_index;
    game.previous_turn = game.current_turn;
    game.previous_pending_turns = game.pending_turns;
}

void handle_play_card(int player_index, Message *msg) {
    Player *p = &game.players[player_index];
    if (!p->alive) {
        send_info(p->socket_fd, "Estás eliminado. No puedes realizar acciones.");
        return;
    }

    if (!game.game_started) {
        send_info(p->socket_fd, "La partida todavía no ha comenzado.");
        return;
    }

    Card selected;
    int found = 0;

    for (int i = 0; i < p->hand_count; i++) {
        if (p->hand[i].id == msg->card_id) {
            selected = p->hand[i];
            found = 1;
            break;
        }
    }

    if (!found) {
        send_info(p->socket_fd, "No tienes esa carta.");
        return;
    }

    if (player_index != game.current_turn && selected.type != CARD_NOPE) {
        send_info(p->socket_fd, "No es tu turno.");
        return;
    }

    if (player_index != game.current_turn && selected.type == CARD_NOPE && !game.has_last_action) {
        send_info(p->socket_fd, "No hay una acción reciente para cancelar con Nope.");
        return;
    }
    
    if (selected.type == CARD_DEFUSE) {
        send_info(p->socket_fd, "No puedes jugar Desactivar manualmente. Se usa automáticamente si robas un Gato Explosivo.");
        return;
    }

    if (selected.type == CARD_EXPLODING) {
        send_info(p->socket_fd, "No puedes jugar un Gato Explosivo.");
        return;
    }

    if (selected.type == CARD_FAVOR) {
        int target = find_player_index_by_id(&game, msg->target_player);

        if (target == -1 ||
            target == player_index ||
            !game.players[target].alive ||
            game.players[target].hand_count == 0) {
            send_info(p->socket_fd, "Jugador objetivo inválido o sin cartas.");
            return;
        }
    }

    Card played;
    remove_card_from_hand(p, msg->card_id, &played);
    game.discard[game.discard_count++] = played;

    char log_action[MAX_TEXT];
    snprintf(log_action, MAX_TEXT,
            "PLAY_CARD carta=%s carta_id=%d",
            card_to_string(played.type),
            played.id);

    write_log(game.server_lamport_clock,
            msg->correlation_id,
            p->id,
            log_action);

    char text[MAX_TEXT];

    if (played.type == CARD_NOPE) {
        if (!game.has_last_action) {
            snprintf(text, MAX_TEXT,
                "Jugador %d jugó Nope, pero no había una acción cancelable.",
                p->id);
            broadcast_info(text);
            broadcast_states();
            return;
        }

        if (game.last_action_card.type == CARD_ATTACK ||
            game.last_action_card.type == CARD_SKIP) {

            game.current_turn = game.previous_turn;
            game.pending_turns = game.previous_pending_turns;

            snprintf(text, MAX_TEXT,
                "Jugador %d jugó Nope y canceló %s.",
                p->id,
                card_to_string(game.last_action_card.type));

        } else if (game.last_action_card.type == CARD_FAVOR) {

            if (game.has_stolen_card &&
                game.last_action_player >= 0 &&
                game.last_target_player >= 0) {

                Card returned;
                remove_card_from_hand(&game.players[game.last_action_player],
                                    game.last_stolen_card.id,
                                    &returned);

                add_card_to_hand(&game.players[game.last_target_player], returned);

                snprintf(text, MAX_TEXT,
                    "Jugador %d jugó Nope y canceló Favor. La carta robada volvió al Jugador %d.",
                    p->id,
                    game.players[game.last_target_player].id);
            } else {
                snprintf(text, MAX_TEXT,
                    "Jugador %d jugó Nope, pero no se pudo revertir Favor.",
                    p->id);
            }

        } else {
            snprintf(text, MAX_TEXT,
                "Jugador %d jugó Nope, pero la última acción no era cancelable.",
                p->id);
        }

        game.has_last_action = 0;
        game.has_stolen_card = 0;

        broadcast_info(text);
        broadcast_states();
        return;
    }

    if (played.type == CARD_SKIP) {
        save_last_action(played, player_index, -1);
        snprintf(text, MAX_TEXT,
            "Jugador %d jugó Saltar. Termina su turno sin robar.",
            p->id);
        broadcast_info(text);
        game.pending_turns--;

        if (game.pending_turns <= 0) {
            game.pending_turns = 1;
            next_turn(&game);
        }
    } else if (played.type == CARD_SEE_FUTURE) {

        snprintf(text, MAX_TEXT, "Usaste Ver el Futuro. Próximas 3 cartas del mazo:\n");

        for (int i = 0; i < 3 && i < game.deck_count; i++) {
            Card c = game.deck[game.deck_count - 1 - i];
            char line[80];
            snprintf(line, sizeof(line), "  %s\n", card_to_string(c.type));
            strncat(text, line, sizeof(text) - strlen(text) - 1);
        }

        send_info(p->socket_fd, text);
    } else if (played.type == CARD_ATTACK) {
        save_last_action(played, player_index, -1);

        snprintf(text, MAX_TEXT,
            "Jugador %d jugó Ataque. El siguiente jugador deberá cumplir 2 turnos.",
            p->id);
        broadcast_info(text);

        game.pending_turns = 2;
        next_turn(&game);
    } else if (played.type == CARD_FAVOR) {
        int target = find_player_index_by_id(&game, msg->target_player);

        if (target == -1 ||
            target == player_index ||
            !game.players[target].alive ||
            game.players[target].hand_count == 0) {
            send_info(p->socket_fd, "Jugador objetivo inválido o sin cartas.");
            return;
        }

        save_last_action(played, player_index, target);

        int random_index = rand() % game.players[target].hand_count;
        Card stolen = game.players[target].hand[random_index];
        remove_card_from_hand(&game.players[target], stolen.id, &stolen);
        add_card_to_hand(p, stolen);

        game.last_stolen_card = stolen;
        game.has_stolen_card = 1;

        snprintf(text, MAX_TEXT,
            "Jugador %d jugó Favor y recibió una carta del Jugador %d.",
            p->id, game.players[target].id);
        broadcast_info(text);
    } else {
        snprintf(text, MAX_TEXT,
            "Jugador %d jugó %s.",
            p->id, card_to_string(played.type));
        broadcast_info(text);
    }

    if (!validate_card_consistency(&game)) {
        broadcast_info("ERROR DE CONSISTENCIA: una carta aparece duplicada.");
    }

    broadcast_states();
}

void start_game() {
    if (game.game_started) {
        broadcast_info("La partida ya está en curso.");
        return;
    }
    if (game.player_count < MIN_PLAYERS) {
        broadcast_info("Se necesitan al menos 2 jugadores para iniciar.");
        return;
    }

    setup_deck(&game);

    for (int i = 0; i < game.player_count; i++) {
        game.players[i].alive = 1;
    }

    deal_initial_cards(&game);

    game.current_turn = 0;
    game.game_started = 1;
    if (log_file != NULL) {
        fprintf(log_file, "\n===== NUEVA PARTIDA =====\n");
        fprintf(log_file, "Jugadores: %d\n", game.player_count);
        fprintf(log_file, "Mazo inicial: %d cartas\n", game.deck_count);
        fflush(log_file);
    }
    game.pending_turns = 1;

    broadcast_info("La partida ha comenzado.");
    broadcast_states();
}

void handle_play_pair(int player_index, Message *msg) {
    Player *p = &game.players[player_index];
    if (!p->alive) {
        send_info(p->socket_fd, "Estás eliminado. No puedes realizar acciones.");
        return;
    }

    if (!game.game_started) {
        send_info(p->socket_fd, "La partida todavía no ha comenzado.");
        return;
    }

    if (player_index != game.current_turn) {
        send_info(p->socket_fd, "No es tu turno.");
        return;
    }

    Card first, second;
    int found_first = 0;
    int found_second = 0;

    for (int i = 0; i < p->hand_count; i++) {
        if (p->hand[i].id == msg->card_id) {
            first = p->hand[i];
            found_first = 1;
        }

        if (p->hand[i].id == msg->second_card_id) {
            second = p->hand[i];
            found_second = 1;
        }
    }

    if (!found_first || !found_second || msg->card_id == msg->second_card_id) {
        send_info(p->socket_fd, "No tienes ambas cartas o repetiste el mismo ID.");
        return;
    }

    if (first.type != second.type) {
        send_info(p->socket_fd, "Para jugar un par, ambas cartas deben ser idénticas.");
        return;
    }

    if (first.type == CARD_EXPLODING ||
        first.type == CARD_DEFUSE ||
        first.type == CARD_SKIP ||
        first.type == CARD_SEE_FUTURE ||
        first.type == CARD_ATTACK ||
        first.type == CARD_FAVOR ||
        first.type == CARD_NOPE) {
        send_info(p->socket_fd, "Por ahora los pares solo se permiten con cartas de gato idénticas.");
        return;
    }

    int target = find_player_index_by_id(&game, msg->target_player);

    if (target == -1 ||
        target == player_index ||
        !game.players[target].alive ||
        game.players[target].hand_count == 0) {
        send_info(p->socket_fd, "Jugador objetivo inválido o sin cartas.");
        return;
    }

    Card removed_first;
    Card removed_second;

    remove_card_from_hand(p, msg->card_id, &removed_first);
    remove_card_from_hand(p, msg->second_card_id, &removed_second);

    game.discard[game.discard_count++] = removed_first;
    game.discard[game.discard_count++] = removed_second;

    int random_index = rand() % game.players[target].hand_count;
    Card stolen = game.players[target].hand[random_index];

    remove_card_from_hand(&game.players[target], stolen.id, &stolen);
    add_card_to_hand(p, stolen);

    char text[MAX_TEXT];
    snprintf(text, MAX_TEXT,
        "Jugador %d jugó un par de Cartas Normales y robó una carta aleatoria al Jugador %d.",
        p->id,
        game.players[target].id);

    broadcast_info(text);

    if (!validate_card_consistency(&game)) {
        broadcast_info("ERROR DE CONSISTENCIA: una carta aparece duplicada.");
    }

    broadcast_states();
}

void send_players_list(int player_index) {
    char text[MAX_TEXT];
    char line[128];

    snprintf(text, sizeof(text), "Jugadores conectados:\n");

    for (int i = 0; i < game.player_count; i++) {
        snprintf(line, sizeof(line),
                 "  Jugador %d: %s | %s\n",
                 game.players[i].id,
                 game.players[i].name,
                 game.players[i].alive ? "Vivo" : "Eliminado");

        strncat(text, line, sizeof(text) - strlen(text) - 1);
    }

    send_info(game.players[player_index].socket_fd, text);
}

void send_help(int player_index) {
    const char *text =
        "Ayuda de comandos:\n"
        "start: inicia la partida si hay al menos 2 jugadores.\n"
        "draw: roba una carta y termina tu turno.\n"
        "play: juega una carta por su ID.\n"
        "playpair: juega 2 Cartas Normales para robar a otro jugador.\n"
        "players: muestra jugadores conectados.\n"
        "exit: salir de la partida.\n";

    send_info(game.players[player_index].socket_fd, text);
}

void *client_thread(void *arg) {
    int player_index = *(int *)arg;
    free(arg);

    Message msg;

    while (recv(game.players[player_index].socket_fd, &msg, sizeof(Message), 0) > 0) {
        pthread_mutex_lock(&game_mutex);

        game.server_lamport_clock =
            (game.server_lamport_clock > msg.lamport_timestamp
                ? game.server_lamport_clock
                : msg.lamport_timestamp) + 1;

        if (msg.type != MSG_ACK &&
            msg.correlation_id == game.players[player_index].last_correlation_id) {
            printf("[L=%d][CID=%d] Mensaje duplicado ignorado desde Jugador %d\n",
                game.server_lamport_clock,
                msg.correlation_id,
                game.players[player_index].id);
            pthread_mutex_unlock(&game_mutex);
            continue;
        }

        if (msg.type != MSG_ACK) {
            game.players[player_index].last_correlation_id = msg.correlation_id;
        }

        printf("[L=%d][CID=%d] Jugador %d envió %s\n",
            game.server_lamport_clock,
            msg.correlation_id,
            game.players[player_index].id,
            message_type_to_string(msg.type));

        write_log(game.server_lamport_clock,
          msg.correlation_id,
          game.players[player_index].id,
          message_type_to_string(msg.type));

        if (msg.type == MSG_START_GAME) {
            start_game();
        } else if (msg.type == MSG_DRAW_CARD) {
            handle_draw(player_index);
        } else if (msg.type == MSG_PLAY_CARD) {
            handle_play_card(player_index, &msg);
        } else if (msg.type == MSG_PLAY_PAIR) {
            handle_play_pair(player_index, &msg);
        } else if (msg.type == MSG_PLAYERS) {
            send_players_list(player_index);
        } else if (msg.type == MSG_HELP) {
            send_help(player_index);
        } else if (msg.type == MSG_ACK) {
            printf("[L=%d][CID=%d] ACK recibido desde Jugador %d\n",
                game.server_lamport_clock,
                msg.correlation_id,
                game.players[player_index].id);
        } else if (msg.type == MSG_EXIT) {
            close(game.players[player_index].socket_fd);
            pthread_mutex_unlock(&game_mutex);
            return NULL;
        }

        pthread_mutex_unlock(&game_mutex);
    }

    pthread_mutex_lock(&game_mutex);

    game.players[player_index].alive = 0;

    char text[MAX_TEXT];
    snprintf(text, MAX_TEXT,
            "Jugador %d se ha desconectado y queda fuera de la partida.",
            game.players[player_index].id);

    broadcast_info(text);

    if (game.game_started && alive_players(&game) <= 1) {
        int winner = get_winner(&game);

        if (winner != -1) {
            snprintf(text, MAX_TEXT,
                    "¡Jugador %d gana la partida por desconexión del resto!",
                    game.players[winner].id);
            broadcast_info(text);
        }

        game.game_started = 0;
    }

    broadcast_states();

    pthread_mutex_unlock(&game_mutex);

    return NULL;
}

void write_log(int lamport, int cid, int player_id, const char *action) {
    if (log_file == NULL) return;

    fprintf(log_file, "[L=%d][CID=%d][Jugador=%d] %s\n",
            lamport, cid, player_id, action);

    fflush(log_file);
}

int main(int argc, char *argv[]) {
    int port = PORT;

    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    init_game(&game);
    srand(time(NULL));

    log_file = fopen("game_log.txt", "w");

    if (log_file == NULL) {
        perror("No se pudo abrir game_log.txt");
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, MAX_PLAYERS) < 0) {
        perror("listen");
        return 1;
    }

    printf("Servidor escuchando en puerto %d...\n", port);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        Message join_msg;
        int bytes = recv(client_fd, &join_msg, sizeof(Message), 0);

        if (bytes != sizeof(Message) || join_msg.type != MSG_JOIN) {
            printf("Conexión rechazada: no es un cliente válido del juego.\n");
            close(client_fd);
            continue;
        }

        char received_name[MAX_NAME];
        char received_password[MAX_NAME];

        memset(received_name, 0, sizeof(received_name));
        memset(received_password, 0, sizeof(received_password));

        sscanf(join_msg.text, "%31[^|]|%31s", received_name, received_password);

        if (strcmp(received_password, ROOM_PASSWORD) != 0) {
            send_info(client_fd, "Clave de sala incorrecta.");
            close(client_fd);
            continue;
        }

        if (game.game_started) {
            send_info(client_fd, "La partida ya comenzó. No puedes unirte ahora.");
            close(client_fd);
            continue;
        }
        
        if (game.player_count >= MAX_PLAYERS) {
            send_info(client_fd, "La sala está llena.");
            close(client_fd);
            continue;
        }

        pthread_mutex_lock(&game_mutex);

        int index = game.player_count;
        game.players[index].id = index;
        game.players[index].socket_fd = client_fd;
        game.players[index].alive = 1;
        game.players[index].last_correlation_id = -1;
        snprintf(game.players[index].name, MAX_NAME, "%s", received_name);

        game.player_count++;

        char text[MAX_TEXT];
        snprintf(text, MAX_TEXT,
            "Jugador %d (%s) se ha conectado.",
            game.players[index].id,
            game.players[index].name);

        printf("%s\n", text);
        broadcast_info(text);
        send_private_state(index);

        pthread_t tid;
        int *arg = malloc(sizeof(int));
        *arg = index;
        pthread_create(&tid, NULL, client_thread, arg);
        pthread_detach(tid);

        pthread_mutex_unlock(&game_mutex);
    }

    if (log_file != NULL) {
    fclose(log_file);
    }

    close(server_fd);
    return 0;
}