#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "game.h"
#include <signal.h>
#include <sys/time.h>

#define PORT 8080
#define BUFFER_SIZE 8192
GameState game;
pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;

void send_file(int client_fd, const char *path, const char *content_type) {
    FILE *file = fopen(path, "rb");

    if (!file) {
        char *not_found =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Connection: close\r\n\r\n"
            "Archivo no encontrado";
        send(client_fd, not_found, strlen(not_found), 0);
        return;
    }

    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Connection: close\r\n"
             "\r\n",
             content_type);

    send(client_fd, header, strlen(header), 0);

    char buffer[8192];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        send(client_fd, buffer, bytes_read, 0);
    }

    fclose(file);
}

void send_json(int client_fd, const char *json) {
    char header[512];

    snprintf(header, sizeof(header),
         "HTTP/1.1 200 OK\r\n"
         "Content-Type: application/json\r\n"
         "Access-Control-Allow-Origin: *\r\n"
         "Connection: close\r\n"
         "\r\n");

    send(client_fd, header, strlen(header), 0);
    send(client_fd, json, strlen(json), 0);
}

void get_query_param(const char *path, const char *key, char *output, int max_len) {
    output[0] = '\0';

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s=", key);

    const char *start = strstr(path, pattern);

    if (!start) return;

    start += strlen(pattern);

    int i = 0;
    while (*start && *start != '&' && i < max_len - 1) {
        output[i++] = *start++;
    }

    output[i] = '\0';
}

void send_state_json(int client_fd, int player_id) {
    char json[8192];
    char temp[512];

    pthread_mutex_lock(&game_mutex);

    if (player_id < 0 || player_id >= game.player_count) {
        pthread_mutex_unlock(&game_mutex);
        send_json(client_fd, "{\"ok\":false,\"error\":\"Jugador inválido\"}");
        return;
    }

    Player *p = &game.players[player_id];

    snprintf(json, sizeof(json),
        "{\"ok\":true,"
        "\"player_id\":%d,"
        "\"name\":\"%s\","
        "\"game_started\":%d,"
        "\"current_turn\":%d,"
        "\"is_my_turn\":%d,"
        "\"deck_count\":%d,"
        "\"pending_turns\":%d,"
        "\"lamport\":%d,"
        "\"last_message\":\"%s\","
        "\"players\":[",
        p->id,
        p->name,
        game.game_started,
        game.players[game.current_turn].id,
        player_id == game.current_turn,
        game.deck_count,
        game.pending_turns,
        game.server_lamport_clock,
        game.last_message
    );

    for (int i = 0; i < game.player_count; i++) {
        snprintf(temp, sizeof(temp),
            "%s{\"id\":%d,\"name\":\"%s\",\"alive\":%d}",
            i == 0 ? "" : ",",
            game.players[i].id,
            game.players[i].name,
            game.players[i].alive);

        strncat(json, temp, sizeof(json) - strlen(json) - 1);
    }

    strncat(json, "],\"hand\":[", sizeof(json) - strlen(json) - 1);

    for (int i = 0; i < p->hand_count; i++) {
        snprintf(temp, sizeof(temp),
            "%s{\"id\":%d,\"type\":%d,\"name\":\"%s\"}",
            i == 0 ? "" : ",",
            p->hand[i].id,
            p->hand[i].type,
            card_to_string(p->hand[i].type));

        strncat(json, temp, sizeof(json) - strlen(json) - 1);
    }

    strncat(json, "]}", sizeof(json) - strlen(json) - 1);

    pthread_mutex_unlock(&game_mutex);

    send_json(client_fd, json);
}

void save_last_action(Card played, int player_index, int target_index) {
    game.last_action_card = played;
    game.has_last_action = 1;
    game.last_action_player = player_index;
    game.last_target_player = target_index;
    game.previous_turn = game.current_turn;
    game.previous_pending_turns = game.pending_turns;
}

void url_decode(char *str) {
    char *src = str;
    char *dst = str;

    while (*src) {
        if (*src == '%' &&
            src[1] == '2' &&
            src[2] == '0') {
            *dst++ = ' ';
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE];

    init_game(&game);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Servidor web escuchando en puerto %d...\n", PORT);

    while (1) {
        client_fd = accept(server_fd,
                           (struct sockaddr *)&address,
                           (socklen_t *)&addrlen);

        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        
        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        memset(buffer, 0, BUFFER_SIZE);
        int received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (received <= 0) {
            close(client_fd);
            continue;
        }

        char method[16];
        char path[256];

        sscanf(buffer, "%15s %255s", method, path);

        printf("Request: %s %s\n", method, path);

        if (strncmp(path, "/api/join", 9) == 0) {
            char name[MAX_NAME];
            char password[MAX_NAME];
            char json[512];

            get_query_param(path, "name", name, MAX_NAME);
            get_query_param(path, "password", password, MAX_NAME);
            url_decode(name);
            url_decode(password);

            pthread_mutex_lock(&game_mutex);

            if (strcmp(password, ROOM_PASSWORD) != 0) {
                snprintf(json, sizeof(json),
                        "{\"ok\":false,\"error\":\"Clave incorrecta\"}");
            } else if (game.game_started) {
                snprintf(json, sizeof(json),
                        "{\"ok\":false,\"error\":\"La partida ya comenzó\"}");
            } else if (game.player_count >= MAX_PLAYERS) {
                snprintf(json, sizeof(json),
                        "{\"ok\":false,\"error\":\"Sala llena\"}");
            } else {
                int index = game.player_count;

                game.players[index].id = index;
                game.players[index].alive = 1;
                game.players[index].socket_fd = -1;
                game.players[index].hand_count = 0;
                game.players[index].last_correlation_id = -1;

                snprintf(game.players[index].name, MAX_NAME, "%s", name);

                game.player_count++;

                snprintf(json, sizeof(json),
                        "{\"ok\":true,\"player_id\":%d,\"name\":\"%s\"}",
                        game.players[index].id,
                        game.players[index].name);
            }

            pthread_mutex_unlock(&game_mutex);

            send_json(client_fd, json);

        } else if (strncmp(path, "/api/playpair", 13) == 0) {
            char id_text[16], card1_text[16], card2_text[16], target_text[16];
            char json[1024];

            get_query_param(path, "player_id", id_text, sizeof(id_text));
            get_query_param(path, "card1_id", card1_text, sizeof(card1_text));
            get_query_param(path, "card2_id", card2_text, sizeof(card2_text));
            get_query_param(path, "target_id", target_text, sizeof(target_text));

            int player_id = atoi(id_text);
            int card1_id = atoi(card1_text);
            int card2_id = atoi(card2_text);
            int target_id = atoi(target_text);

            pthread_mutex_lock(&game_mutex);

            if (player_id < 0 || player_id >= game.player_count) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Jugador inválido\"}");
            } else if (!game.game_started) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"La partida no ha comenzado\"}");
            } else if (player_id != game.current_turn) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"No es tu turno\"}");
            } else if (card1_id == card2_id) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Debes elegir dos cartas distintas\"}");
            } else {
                Player *p = &game.players[player_id];
                Card first, second;
                int found_first = 0, found_second = 0;

                for (int i = 0; i < p->hand_count; i++) {
                    if (p->hand[i].id == card1_id) {
                        first = p->hand[i];
                        found_first = 1;
                    }
                    if (p->hand[i].id == card2_id) {
                        second = p->hand[i];
                        found_second = 1;
                    }
                }

                if (!found_first || !found_second) {
                    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"No tienes ambas cartas\"}");
                } else if (first.type != second.type) {
                    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Las cartas deben ser idénticas\"}");
                } else if (!(first.type == CARD_TACOCAT ||
                            first.type == CARD_BEARD_CAT ||
                            first.type == CARD_RAINBOW_CAT ||
                            first.type == CARD_POTATO_CAT ||
                            first.type == CARD_CATERMELON)) {
                    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Solo puedes jugar pares de cartas de gato\"}");
                } else if (target_id < 0 || target_id >= game.player_count ||
                        target_id == player_id ||
                        !game.players[target_id].alive ||
                        game.players[target_id].hand_count == 0) {
                    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Jugador objetivo inválido\"}");
                } else {
                    Card removed_first, removed_second;

                    remove_card_from_hand(p, card1_id, &removed_first);
                    remove_card_from_hand(p, card2_id, &removed_second);

                    game.discard[game.discard_count++] = removed_first;
                    game.discard[game.discard_count++] = removed_second;

                    save_last_action(first, player_id, target_id);

                    int random_index = rand() % game.players[target_id].hand_count;
                    Card stolen = game.players[target_id].hand[random_index];

                    remove_card_from_hand(&game.players[target_id], stolen.id, &stolen);
                    add_card_to_hand(p, stolen);

                    game.last_stolen_card = stolen;
                    game.has_stolen_card = 1;

                    snprintf(game.last_message, MAX_TEXT,
                        "%s jugó un par y robó una carta a %s.",
                        p->name,
                        game.players[target_id].name);

                    snprintf(json, sizeof(json),
                            "{\"ok\":true,\"message\":\"Jugaste un par y robaste una carta al Jugador %d\"}",
                            target_id);
                }
            }

            pthread_mutex_unlock(&game_mutex);
            send_json(client_fd, json);

        } else if (strncmp(path, "/api/play", 9) == 0) {
            char id_text[16];
            char card_text[16];
            char target_text[16];
            char json[1024];

            get_query_param(path, "player_id", id_text, sizeof(id_text));
            get_query_param(path, "card_id", card_text, sizeof(card_text));
            get_query_param(path, "target_id", target_text, sizeof(target_text));

            int player_id = atoi(id_text);
            int card_id = atoi(card_text);
            int target_id = atoi(target_text);

            pthread_mutex_lock(&game_mutex);

            if (player_id < 0 || player_id >= game.player_count) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Jugador inválido\"}");
            } else if (!game.game_started) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"La partida no ha comenzado\"}");
            } else {
                Player *p = &game.players[player_id];

                Card selected;
                int found = 0;

                for (int i = 0; i < p->hand_count; i++) {
                    if (p->hand[i].id == card_id) {
                        selected = p->hand[i];
                        found = 1;
                        break;
                    }
                }

                if (!found) {
                    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"No tienes esa carta\"}");
                } else if (!p->alive) {
                    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Estás eliminado\"}");
                } else if (player_id != game.current_turn && selected.type != CARD_NOPE) {
                    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"No es tu turno\"}");
                } else if (selected.type == CARD_DEFUSE) {
                    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Desactivar se usa automáticamente\"}");
                } else if (selected.type == CARD_EXPLODING) {
                    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"No puedes jugar un Gato Explosivo\"}");
                } else if (selected.type == CARD_NOPE && !game.has_last_action) {
                    snprintf(json, sizeof(json),
                            "{\"ok\":false,\"error\":\"No hay una acción cancelable\"}");
                } else if (selected.type == CARD_TACOCAT ||
                        selected.type == CARD_BEARD_CAT ||
                        selected.type == CARD_RAINBOW_CAT ||
                        selected.type == CARD_POTATO_CAT ||
                        selected.type == CARD_CATERMELON) {
                    snprintf(json, sizeof(json),
                            "{\"ok\":false,\"error\":\"Las cartas de gato solo se juegan en pares con otra idéntica\"}");
                } else {
                    Card played;
                    remove_card_from_hand(p, card_id, &played);
                    game.discard[game.discard_count++] = played;

                    if (played.type == CARD_SKIP) {
                        save_last_action(played, player_id, -1);
                        game.pending_turns--;

                        if (game.pending_turns <= 0) {
                            game.pending_turns = 1;
                            next_turn(&game);
                        }

                        snprintf(game.last_message, MAX_TEXT,
                            "%s jugó Saltar.",
                            p->name);                        

                        snprintf(json, sizeof(json),
                                "{\"ok\":true,\"message\":\"Jugaste Saltar\"}");

                    } else if (played.type == CARD_SEE_FUTURE) {
                        char future[256] = "";

                        int count = game.deck_count < 3 ? game.deck_count : 3;

                        for (int i = 0; i < count; i++) {
                            Card c = game.deck[game.deck_count - 1 - i];

                            strcat(future, card_to_string(c.type));

                            if (i < count - 1) {
                                strcat(future, ", ");
                            }
                        }

                        snprintf(json, sizeof(json),
                                "{\"ok\":true,\"message\":\"Próximas cartas: %s\"}",
                                future);

                    } else if (played.type == CARD_ATTACK) {
                        save_last_action(played, player_id, -1);
                        game.pending_turns = 2;
                        next_turn(&game);

                        snprintf(game.last_message, MAX_TEXT,
                            "%s jugó Ataque. El siguiente jugador tiene 2 turnos.",
                            p->name);

                        snprintf(json, sizeof(json),
                                "{\"ok\":true,\"message\":\"Jugaste Ataque. El siguiente jugador tiene 2 turnos\"}");

                    } else if (played.type == CARD_FAVOR) {
                        int target = target_id;

                        save_last_action(played, player_id, target);

                        if (target < 0 || target >= game.player_count ||
                            target == player_id ||
                            !game.players[target].alive ||
                            game.players[target].hand_count == 0) {

                            add_card_to_hand(p, played);
                            game.discard_count--;

                            snprintf(json, sizeof(json),
                                    "{\"ok\":false,\"error\":\"Jugador objetivo inválido\"}");
                        } else {
                            int random_index = rand() % game.players[target].hand_count;
                            Card stolen = game.players[target].hand[random_index];

                            remove_card_from_hand(&game.players[target], stolen.id, &stolen);
                            add_card_to_hand(p, stolen);

                            game.last_stolen_card = stolen;
                            game.has_stolen_card = 1;

                            snprintf(game.last_message, MAX_TEXT,
                                "%s jugó Favor contra %s.",
                                p->name,
                                game.players[target].name);

                            snprintf(json, sizeof(json),
                                    "{\"ok\":true,\"message\":\"Jugaste Favor y recibiste una carta\"}");
                        }

                    } else if (played.type == CARD_NOPE) {
                        if (!game.has_last_action) {
                            snprintf(json, sizeof(json),
                                    "{\"ok\":false,\"error\":\"No hay una acción cancelable\"}");
                        } else if (game.last_action_card.type == CARD_ATTACK ||
                                game.last_action_card.type == CARD_SKIP) {

                            game.current_turn = game.previous_turn;
                            game.pending_turns = game.previous_pending_turns;

                            game.has_last_action = 0;
                            game.has_stolen_card = 0;

                            snprintf(game.last_message, MAX_TEXT,
                                "%s jugó Nope y canceló la acción anterior.",
                                p->name);

                            snprintf(json, sizeof(json),
                                    "{\"ok\":true,\"message\":\"Jugaste Nope y cancelaste la acción anterior\"}");

                        } else if (game.last_action_card.type == CARD_FAVOR ||
                                game.last_action_card.type == CARD_TACOCAT ||
                                game.last_action_card.type == CARD_BEARD_CAT ||
                                game.last_action_card.type == CARD_RAINBOW_CAT ||
                                game.last_action_card.type == CARD_POTATO_CAT ||
                                game.last_action_card.type == CARD_CATERMELON) {

                            if (game.has_stolen_card &&
                                game.last_action_player >= 0 &&
                                game.last_target_player >= 0) {

                                Card returned;
                                remove_card_from_hand(&game.players[game.last_action_player],
                                                    game.last_stolen_card.id,
                                                    &returned);

                                add_card_to_hand(&game.players[game.last_target_player], returned);

                                game.has_last_action = 0;
                                game.has_stolen_card = 0;

                                snprintf(game.last_message, MAX_TEXT,
                                    "%s jugó Nope y devolvió la carta robada.",
                                    p->name);

                                snprintf(json, sizeof(json),
                                        "{\"ok\":true,\"message\":\"Jugaste Nope y la carta robada fue devuelta\"}");
                            } else {
                                snprintf(json, sizeof(json),
                                        "{\"ok\":false,\"error\":\"No se pudo revertir la acción\"}");
                            }

                        } else {
                            snprintf(json, sizeof(json),
                                    "{\"ok\":false,\"error\":\"La última acción no es cancelable\"}");
                        }
                    } else {
                        snprintf(json, sizeof(json),
                                "{\"ok\":true,\"message\":\"Jugaste %s\"}",
                                card_to_string(played.type));
                    }
                }
            }

            pthread_mutex_unlock(&game_mutex);

            send_json(client_fd, json);

        } else if (strncmp(path, "/api/draw", 9) == 0) {
            char id_text[16];
            char json[1024];

            get_query_param(path, "player_id", id_text, sizeof(id_text));
            int player_id = atoi(id_text);

            pthread_mutex_lock(&game_mutex);

            if (player_id < 0 || player_id >= game.player_count) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Jugador inválido\"}");
            } else if (!game.game_started) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"La partida no ha comenzado\"}");
            } else if (!game.players[player_id].alive) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Estás eliminado\"}");
            } else if (player_id != game.current_turn) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"No es tu turno\"}");
            } else if (game.deck_count <= 0) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"El mazo está vacío\"}");
            } else {
                Card card = draw_card(&game);
                Player *p = &game.players[player_id];

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

                        game.pending_turns--;

                        if (game.pending_turns <= 0) {
                            game.pending_turns = 1;
                            next_turn(&game);
                        }

                        snprintf(game.last_message, MAX_TEXT,
                            "%s robó un Gato Explosivo, pero usó Desactivar.",
                            p->name);

                        snprintf(json, sizeof(json),
                                "{\"ok\":true,\"message\":\"Robaste un Gato Explosivo, pero usaste Desactivar\"}");
                    } else {
                        p->alive = 0;
                        game.out_of_game[game.out_count++] = card;

                        if (alive_players(&game) <= 1) {
                            int winner = get_winner(&game);
                            game.game_started = 0;

                            snprintf(json, sizeof(json),
                                    "{\"ok\":true,\"message\":\"Robaste un Gato Explosivo y fuiste eliminado. Ganó el Jugador %d\"}",
                                    game.players[winner].id);
                        } else {
                            game.pending_turns--;

                            if (game.pending_turns <= 0) {
                                game.pending_turns = 1;
                                next_turn(&game);
                            }

                            snprintf(game.last_message, MAX_TEXT,
                                "%s robó un Gato Explosivo y fue eliminado.",
                                p->name);

                            snprintf(json, sizeof(json),
                                    "{\"ok\":true,\"message\":\"Robaste un Gato Explosivo y fuiste eliminado\"}");
                        }
                    }
                } else {
                    add_card_to_hand(p, card);

                    game.pending_turns--;

                    if (game.pending_turns <= 0) {
                        game.pending_turns = 1;
                        next_turn(&game);
                    }
                    snprintf(game.last_message, MAX_TEXT,
                        "%s robó una carta.",
                        p->name);

                    snprintf(json, sizeof(json),
                            "{\"ok\":true,\"message\":\"Robaste una carta: %s\"}",
                            card_to_string(card.type));
                }
            }

            pthread_mutex_unlock(&game_mutex);

            send_json(client_fd, json);

        } else if (strncmp(path, "/api/start", 10) == 0) {
            char id_text[16];
            char json[512];

            get_query_param(path, "player_id", id_text, sizeof(id_text));
            int player_id = atoi(id_text);

            pthread_mutex_lock(&game_mutex);

            if (player_id < 0 || player_id >= game.player_count) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Jugador inválido\"}");
            } else if (game.game_started) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"La partida ya comenzó\"}");
            } else if (game.player_count < MIN_PLAYERS) {
                snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"Se necesitan al menos 2 jugadores\"}");
            } else {
                setup_deck(&game);

                for (int i = 0; i < game.player_count; i++) {
                    game.players[i].alive = 1;
                }

                deal_initial_cards(&game);

                game.current_turn = 0;
                game.pending_turns = 1;
                game.game_started = 1;

                snprintf(game.last_message, MAX_TEXT,
                    "La partida ha comenzado.");

                snprintf(json, sizeof(json), "{\"ok\":true,\"message\":\"Partida iniciada\"}");
            }

            pthread_mutex_unlock(&game_mutex);

            send_json(client_fd, json);

        } else if (strncmp(path, "/api/state", 10) == 0) {
            char id_text[16];
            get_query_param(path, "player_id", id_text, sizeof(id_text));

            int player_id = atoi(id_text);
            send_state_json(client_fd, player_id);

        } else if (strcmp(path, "/api/ping") == 0) {
            char *response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{\"message\":\"Servidor C respondiendo correctamente\"}";

            send(client_fd, response, strlen(response), 0);

        } else if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
            send_file(client_fd, "web/index.html", "text/html");

        } else if (strcmp(path, "/styles.css") == 0) {
            send_file(client_fd, "web/styles.css", "text/css");

        } else if (strcmp(path, "/app.js") == 0) {
            send_file(client_fd, "web/app.js", "application/javascript");

        } else if (strncmp(path, "/cards/", 7) == 0) {
            char filepath[512];
            snprintf(filepath, sizeof(filepath), "web%s", path);

            send_file(client_fd, filepath, "image/png");
        } else {
            char *not_found =
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain\r\n\r\n"
                "Ruta no encontrada";
            send(client_fd, not_found, strlen(not_found), 0);
        }
        close(client_fd);
    }

    close(server_fd);
    return 0;
}