#ifndef GAME_H
#define GAME_H

#include "protocol.h"

#define MAX_CARDS 56
#define HAND_SIZE 32

typedef struct {
    int id;
    char name[MAX_NAME];
    int socket_fd;
    int alive;
    int last_correlation_id;
    Card hand[HAND_SIZE];
    int hand_count;
} Player;

typedef struct {
    Player players[MAX_PLAYERS];
    int player_count;

    Card deck[MAX_CARDS];
    int deck_count;

    Card discard[MAX_CARDS];
    int discard_count;

    Card out_of_game[MAX_CARDS];
    int out_count;

    int current_turn;
    int direction;
    int game_started;
    int pending_turns;
    int server_lamport_clock;
    int next_card_id;
    char last_message[MAX_TEXT];
    Card last_action_card;
    int has_last_action;
    int last_action_player;
    int last_target_player;
    Card last_stolen_card;
    int has_stolen_card;
    int previous_turn;
    int previous_pending_turns;
} GameState;

void init_game(GameState *game);
void setup_deck(GameState *game);
void shuffle_deck(GameState *game);
void deal_initial_cards(GameState *game);

const char *card_to_string(CardType type);
Card create_card(GameState *game, CardType type);

void add_card_to_hand(Player *player, Card card);
int remove_card_from_hand(Player *player, int card_id, Card *removed);
Card draw_card(GameState *game);

int alive_players(GameState *game);
int get_winner(GameState *game);
void next_turn(GameState *game);

int validate_card_consistency(GameState *game);

int find_player_index_by_id(GameState *game, int player_id);
#endif
