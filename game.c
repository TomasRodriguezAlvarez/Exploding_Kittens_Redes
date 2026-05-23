#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game.h"

void init_game(GameState *game) {
    memset(game, 0, sizeof(GameState));
    game->current_turn = 0;
    game->direction = 1;
    game->game_started = 0;
    game->pending_turns = 1;
    game->server_lamport_clock = 0;
    game->next_card_id = 1;
    game->has_last_action = 0;
    game->last_action_card.id = -1;
    game->last_action_player = -1;
    game->last_target_player = -1;
    game->has_stolen_card = 0;
    game->previous_turn = 0;
    game->previous_pending_turns = 1;
    game->last_stolen_card.id = -1;
    snprintf(game->last_message, MAX_TEXT, "Esperando inicio de partida.");
}

const char *card_to_string(CardType type) {
    switch (type) {
        case CARD_EXPLODING: return "Gato Explosivo";
        case CARD_DEFUSE: return "Desactivar";
        case CARD_SKIP: return "Saltar";
        case CARD_SEE_FUTURE: return "Ver el Futuro";
        case CARD_ATTACK: return "Ataque";
        case CARD_FAVOR: return "Favor";
        case CARD_NOPE: return "Nope";
        case CARD_TACOCAT: return "Tacocat";
        case CARD_BEARD_CAT: return "Gato Barbudo";
        case CARD_RAINBOW_CAT: return "Gato Arcoiris";
        case CARD_POTATO_CAT: return "Gato Papa Peluda";
        case CARD_CATERMELON: return "Gato Sandía";
        default: return "Carta Desconocida";
    }
}

Card create_card(GameState *game, CardType type) {
    Card card;
    card.id = game->next_card_id++;
    card.type = type;
    return card;
}

void setup_deck(GameState *game) {
    game->deck_count = 0;
    game->discard_count = 0;
    game->out_count = 0;
    game->next_card_id = 1;

    int exploding_count = game->player_count - 1;

    for (int i = 0; i < 4; i++) {
        if (i < exploding_count) {
            game->deck[game->deck_count++] = create_card(game, CARD_EXPLODING);
        } else {
            game->out_of_game[game->out_count++] = create_card(game, CARD_EXPLODING);
        }
    }

    for (int i = 0; i < 6; i++) {
        game->deck[game->deck_count++] = create_card(game, CARD_DEFUSE);
    }

    for (int i = 0; i < 4; i++) {
        game->deck[game->deck_count++] = create_card(game, CARD_SKIP);
        game->deck[game->deck_count++] = create_card(game, CARD_SEE_FUTURE);
        game->deck[game->deck_count++] = create_card(game, CARD_ATTACK);
        game->deck[game->deck_count++] = create_card(game, CARD_FAVOR);
        game->deck[game->deck_count++] = create_card(game, CARD_NOPE);
    }

    CardType normal_cards[] = {
        CARD_TACOCAT,
        CARD_BEARD_CAT,
        CARD_RAINBOW_CAT,
        CARD_POTATO_CAT,
        CARD_CATERMELON
    };

    int normal_index = 0;

    while (game->deck_count + game->out_count < MAX_CARDS) {
        game->deck[game->deck_count++] =
            create_card(game, normal_cards[normal_index]);

        normal_index = (normal_index + 1) % 5;
    }

    shuffle_deck(game);
}

void shuffle_deck(GameState *game) {
    srand(time(NULL));

    for (int i = game->deck_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);

        Card temp = game->deck[i];
        game->deck[i] = game->deck[j];
        game->deck[j] = temp;
    }
}

void add_card_to_hand(Player *player, Card card) {
    if (player->hand_count >= HAND_SIZE) {
        return;
    }

    player->hand[player->hand_count++] = card;
}

int remove_card_from_hand(Player *player, int card_id, Card *removed) {
    for (int i = 0; i < player->hand_count; i++) {
        if (player->hand[i].id == card_id) {
            *removed = player->hand[i];

            for (int j = i; j < player->hand_count - 1; j++) {
                player->hand[j] = player->hand[j + 1];
            }

            player->hand_count--;
            return 1;
        }
    }

    return 0;
}

Card draw_card(GameState *game) {
    Card empty;
    empty.id = -1;
    empty.type = CARD_TACOCAT;

    if (game->deck_count <= 0) {
        return empty;
    }

    return game->deck[--game->deck_count];
}

void deal_initial_cards(GameState *game) {
    for (int i = 0; i < game->player_count; i++) {
        game->players[i].hand_count = 0;

        Card defuse;
        int found_defuse = 0;

        for (int j = 0; j < game->deck_count; j++) {
            if (game->deck[j].type == CARD_DEFUSE) {
                defuse = game->deck[j];

                for (int k = j; k < game->deck_count - 1; k++) {
                    game->deck[k] = game->deck[k + 1];
                }

                game->deck_count--;
                found_defuse = 1;
                break;
            }
        }

        if (found_defuse) {
            add_card_to_hand(&game->players[i], defuse);
        }

        for (int j = 0; j < 4; j++) {
            Card card = draw_card(game);

            if (card.id != -1 && card.type != CARD_EXPLODING) {
                add_card_to_hand(&game->players[i], card);
            } else if (card.type == CARD_EXPLODING) {
                game->deck[game->deck_count++] = card;
                shuffle_deck(game);
                j--;
            }
        }
    }

    shuffle_deck(game);
}

int alive_players(GameState *game) {
    int count = 0;

    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i].alive) {
            count++;
        }
    }

    return count;
}

int get_winner(GameState *game) {
    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i].alive) {
            return i;
        }
    }

    return -1;
}

void next_turn(GameState *game) {
    if (alive_players(game) <= 1) {
        return;
    }

    int next = game->current_turn;

    do {
        next = (next + game->direction + game->player_count) % game->player_count;
    } while (!game->players[next].alive);

    game->current_turn = next;
}

int validate_card_consistency(GameState *game) {
    int seen[MAX_CARDS + 10];
    memset(seen, 0, sizeof(seen));

    for (int i = 0; i < game->deck_count; i++) {
        int id = game->deck[i].id;
        if (id > 0 && id < MAX_CARDS + 10) {
            if (seen[id]) return 0;
            seen[id] = 1;
        }
    }

    for (int i = 0; i < game->discard_count; i++) {
        int id = game->discard[i].id;
        if (id > 0 && id < MAX_CARDS + 10) {
            if (seen[id]) return 0;
            seen[id] = 1;
        }
    }

    for (int i = 0; i < game->out_count; i++) {
        int id = game->out_of_game[i].id;
        if (id > 0 && id < MAX_CARDS + 10) {
            if (seen[id]) return 0;
            seen[id] = 1;
        }
    }

    for (int p = 0; p < game->player_count; p++) {
        for (int c = 0; c < game->players[p].hand_count; c++) {
            int id = game->players[p].hand[c].id;
            if (id > 0 && id < MAX_CARDS + 10) {
                if (seen[id]) return 0;
                seen[id] = 1;
            }
        }
    }

    return 1;
}

int find_player_index_by_id(GameState *game, int player_id) {
    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i].id == player_id) {
            return i;
        }
    }

    return -1;
}

