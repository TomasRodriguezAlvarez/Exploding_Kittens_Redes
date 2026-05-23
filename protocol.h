#ifndef PROTOCOL_H
#define PROTOCOL_H

#define MAX_PLAYERS 5
#define MIN_PLAYERS 2
#define MAX_NAME 32
#define MAX_TEXT 2048
#define PORT 8080
#define ROOM_PASSWORD "gatitos"

typedef enum {
    MSG_JOIN = 1,
    MSG_START_GAME,
    MSG_PLAY_CARD,
    MSG_DRAW_CARD,
    MSG_GAME_STATE,
    MSG_ERROR,
    MSG_INFO,
    MSG_EXIT,
    MSG_ACK,
    MSG_PLAY_PAIR,
    MSG_PLAYERS,
    MSG_HELP
} MessageType;

typedef enum {
    CARD_EXPLODING = 1,
    CARD_DEFUSE,
    CARD_SKIP,
    CARD_SEE_FUTURE,
    CARD_ATTACK,
    CARD_FAVOR,
    CARD_NOPE,
    CARD_TACOCAT,
    CARD_BEARD_CAT,
    CARD_RAINBOW_CAT,
    CARD_POTATO_CAT,
    CARD_CATERMELON
} CardType;

typedef struct {
    int id;
    CardType type;
} Card;

typedef struct {
    MessageType type;
    int player_id;
    int lamport_timestamp;
    int correlation_id;

    int card_id;
    int second_card_id;
    int card_type;
    int target_player;

    char text[MAX_TEXT];
} Message;

#endif