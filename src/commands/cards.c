/** @file cards.c @brief In-memory dealer-controlled multiplayer card tables. */
#include "commands.h"
#include "modes.h"
#include "numerics.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CARD_TABLE_LIMIT 32U
#define CARD_DECK_LIMIT 6U
#define CARD_TOTAL_MAX (52U * CARD_DECK_LIMIT)
#define CARD_PLAYER_LIMIT 64U

typedef struct CardPlayer {
    uint64_t id;
    unsigned short hand[CARD_TOTAL_MAX];
    unsigned int count;
} CardPlayer;
typedef struct CardTable {
    char channel[IRC_CHANNEL_NAME_MAX + 1U];
    uint64_t dealer_id;
    unsigned short deck[CARD_TOTAL_MAX];
    unsigned int total, next, played, discarded;
    CardPlayer players[CARD_PLAYER_LIMIT];
    unsigned int player_count;
    int active;
} CardTable;
static CardTable tables[CARD_TABLE_LIMIT];
static unsigned int cards_rng_state;

static unsigned int cards_random(unsigned int upper) {
    unsigned int x;
    if (cards_rng_state == 0U) {
        cards_rng_state = (unsigned int)time(NULL) ^ (unsigned int)clock() ^ 0x9e3779b9U;
        if (cards_rng_state == 0U) cards_rng_state = 1U;
    }
    x = cards_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    cards_rng_state = x;
    return x % upper;
}
static void card_code(unsigned short card, char out[3]) {
    static const char ranks[] = "A23456789TJQK";
    static const char suits[] = "SHDC";
    out[0] = ranks[(card % 52U) % 13U];
    out[1] = suits[(card % 52U) / 13U];
    out[2] = '\0';
}
static int parse_count(const char *s, unsigned int maximum, unsigned int *value) {
    unsigned long n;
    char *end;
    if (s == NULL || *s == '\0' || *s == '+' || *s == '-') return 0;
    errno = 0;
    n = strtoul(s, &end, 10);
    if (errno || *end || n == 0UL || n > maximum) return 0;
    *value = (unsigned int)n;
    return 1;
}
static void reply(Server *s, Client *to, const char *channel,
                  const char *key, const char *message) {
    if (to != NULL && to->registered)
        client_sendf(to, RPL_KEYVALUE, s->config.server_name,
                     to->nick, channel, key, "*", message);
}
static void broadcast(Server *s, Channel *channel, const char *key, const char *message) {
    ChannelMember *m;
    for (m = channel->members; m != NULL; m = m->next)
        reply(s, m->client, channel->name, key, message);
}
static CardTable *table_find(const char *channel) {
    unsigned int i;
    for (i = 0; i < CARD_TABLE_LIMIT; ++i)
        if (tables[i].active && strcmp(tables[i].channel, channel) == 0) return &tables[i];
    return NULL;
}
void cards_channel_removed(const char *channel) {
    CardTable *t = table_find(channel);
    if (t != NULL) memset(t, 0, sizeof(*t));
}
void cards_reset_all(void) { memset(tables, 0, sizeof(tables)); }
static CardPlayer *player_find(CardTable *t, uint64_t id, int create) {
    unsigned int i;
    for (i = 0; i < t->player_count; ++i)
        if (t->players[i].id == id) return &t->players[i];
    if (!create || t->player_count >= CARD_PLAYER_LIMIT) return NULL;
    i = t->player_count++;
    t->players[i].id = id;
    t->players[i].count = 0U;
    return &t->players[i];
}
static void shuffle(CardTable *t, unsigned int decks) {
    unsigned int i;
    t->total = decks * 52U;
    t->next = t->played = t->discarded = t->player_count = 0U;
    memset(t->players, 0, sizeof(t->players));
    for (i = 0; i < t->total; ++i) t->deck[i] = (unsigned short)i;
    for (i = t->total; i > 1U; --i) {
        unsigned int j = cards_random(i);
        unsigned short temp = t->deck[i-1U];
        t->deck[i-1U] = t->deck[j];
        t->deck[j] = temp;
    }
}
static void error(Server *s, Client *c, const char *message) {
    reply(s, c, c->nick, "CARDS", message);
}
static int is_dealer(Server *s, Client *c, CardTable *t) {
    if (c->id == t->dealer_id ||
        client_mode_has(c->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) return 1;
    error(s, c, "Only the dealer or an IRC operator may do that");
    return 0;
}
static int has_room(Channel *channel, Client *c) {
    return channel_has_client(channel, c);
}
static void hand_send(Server *s, Client *c, CardTable *t, CardPlayer *p) {
    char message[IRCD_MESSAGE_BUFFER_SIZE];
    unsigned int i;
    size_t used = 0U;
    message[0] = '\0';
    if (p != NULL) for (i = 0; i < p->count; ++i) {
        char code[3];
        int n;
        card_code(p->hand[i], code);
        n = snprintf(message + used, sizeof(message) - used,
                     "%s%s", used ? " " : "", code);
        if (n < 0 || (size_t)n >= sizeof(message) - used) break;
        used += (size_t)n;
    }
    /* Long hands are shown in chunks so that IRC lines stay bounded. */
    if (used == 0U) reply(s, c, t->channel, "HAND", "(empty)");
    else {
        char *cursor = message;
        while (*cursor) {
            size_t length = strlen(cursor);
            if (length > 280U) {
                length = 280U;
                while (length > 0U && cursor[length] != ' ') --length;
                if (length == 0U) length = 280U;
            }
            {
                char saved = cursor[length];
                cursor[length] = '\0';
                reply(s, c, t->channel, "HAND", cursor);
                cursor[length] = saved;
                cursor += length;
                while (*cursor == ' ') ++cursor;
            }
        }
    }
}
CommandResult command_cards(Server *s, Client *c, char *params) {
    char *args[8] = {0};
    unsigned int argc = 0U, n, i;
    Channel *channel;
    CardTable *t;
    CardPlayer *p;
    char message[384];
    if (command_require_registered(c)) return COMMAND_KEEP_CLIENT;
    if (params != NULL) {
        char *token = strtok(params, " ");
        while (token != NULL && argc < 8U) {
            args[argc++] = token;
            token = strtok(NULL, " ");
        }
        if (token != NULL) { error(s, c, "Too many arguments"); return COMMAND_KEEP_CLIENT; }
    }
    if (argc < 2U) { error(s, c, "Usage: CARDS <NEW|REQUEST|ASK|DEAL|HAND|PLAY|DISCARD|LEFT|STATUS|SHUFFLE|END> #channel ..."); return COMMAND_KEEP_CLIENT; }
    channel = hash_get(&s->channels_by_name, args[1]);
    if (channel == NULL) {
        client_sendf(c, ERR_NOSUCHCHANNEL, s->config.server_name, c->nick, args[1]);
        return COMMAND_KEEP_CLIENT;
    }
    if (!has_room(channel, c)) {
        client_sendf(c, ERR_NOTONCHANNEL, s->config.server_name, c->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }
    t = table_find(channel->name);
    if (strcasecmp(args[0], "NEW") == 0) {
        unsigned int decks = 1U;
        if (argc > 3U || (argc == 3U && !parse_count(args[2], CARD_DECK_LIMIT, &decks))) {
            error(s, c, "Usage: CARDS NEW #channel [decks:1-6]"); return COMMAND_KEEP_CLIENT;
        }
        if (t != NULL) { error(s, c, "A deck already exists; use END first"); return COMMAND_KEEP_CLIENT; }
        for (i = 0U; i < CARD_TABLE_LIMIT && tables[i].active; ++i) {}
        if (i == CARD_TABLE_LIMIT) { error(s, c, "Too many active card tables"); return COMMAND_KEEP_CLIENT; }
        t = &tables[i];
        memset(t, 0, sizeof(*t));
        t->active = 1;
        t->dealer_id = c->id;
        (void)snprintf(t->channel, sizeof(t->channel), "%s", channel->name);
        shuffle(t, decks);
        (void)snprintf(message, sizeof(message), "%s opened a %u-deck table (%u cards)",
                       c->nick, decks, t->total);
        broadcast(s, channel, "NEW", message);
        return COMMAND_KEEP_CLIENT;
    }
    if (t == NULL) { error(s, c, "No active card table in this channel"); return COMMAND_KEEP_CLIENT; }
    if (strcasecmp(args[0], "REQUEST") == 0 || strcasecmp(args[0], "ASK") == 0) {
        Client *dealer;
        if (argc != 3U || !parse_count(args[2], CARD_TOTAL_MAX, &n)) {
            error(s, c, "Usage: CARDS REQUEST #channel <count>"); return COMMAND_KEEP_CLIENT;
        }
        dealer = server_find_client_by_id(s, t->dealer_id);
        if (dealer == NULL || !dealer->registered) { error(s, c, "Dealer is offline"); return COMMAND_KEEP_CLIENT; }
        (void)snprintf(message, sizeof(message), "%s requests %u card(s)", c->nick, n);
        reply(s, dealer, channel->name, "REQUEST", message);
        reply(s, c, channel->name, "REQUEST", "Request sent to dealer; no cards drawn");
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "DEAL") == 0) {
        Client *target;
        char cards[384];
        size_t used = 0U;
        if (argc != 4U || !parse_count(args[3], 100U, &n)) {
            error(s, c, "Usage: CARDS DEAL #channel <nick> <count:1-100>"); return COMMAND_KEEP_CLIENT;
        }
        if (!is_dealer(s, c, t)) return COMMAND_KEEP_CLIENT;
        target = hash_get(&s->clients_by_nick, args[2]);
        if (target == NULL || !target->registered || !has_room(channel, target)) {
            error(s, c, "Target must be an online member of this channel"); return COMMAND_KEEP_CLIENT;
        }
        p = player_find(t, target->id, 0);
        if (t->total - t->next < n || (p != NULL && p->count + n > CARD_TOTAL_MAX) ||
            (p == NULL && t->player_count == CARD_PLAYER_LIMIT)) {
            error(s, c, "Not enough cards, or player hand limit reached"); return COMMAND_KEEP_CLIENT;
        }
        p = player_find(t, target->id, 1);
        cards[0] = '\0';
        for (i = 0U; i < n; ++i) {
            char code[3];
            int written;
            unsigned short card = t->deck[t->next++];
            p->hand[p->count++] = card;
            card_code(card, code);
            written = snprintf(cards + used, sizeof(cards) - used, "%s%s",
                               i ? " " : "", code);
            if (written < 0 || (size_t)written >= sizeof(cards) - used) break;
            used += (size_t)written;
        }
        /* HAND is the authoritative full private list; DEAL may be truncated
           when a large count is requested. */
        if (i == n) reply(s, target, channel->name, "DEAL", cards);
        else reply(s, target, channel->name, "DEAL", "Cards dealt; use CARDS HAND to see all");
        (void)snprintf(message, sizeof(message), "%s received %u card(s) (%u remaining)",
                       target->nick, n, t->total - t->next);
        broadcast(s, channel, "DEAL", message);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "HAND") == 0 && argc == 2U) {
        hand_send(s, c, t, player_find(t, c->id, 0));
        return COMMAND_KEEP_CLIENT;
    }
    if ((strcasecmp(args[0], "PLAY") == 0 || strcasecmp(args[0], "DISCARD") == 0) && argc == 3U) {
        int is_play = strcasecmp(args[0], "PLAY") == 0;
        char code[3];
        p = player_find(t, c->id, 0);
        if (p == NULL) { error(s, c, "You do not have that card"); return COMMAND_KEEP_CLIENT; }
        for (i = 0U; i < p->count; ++i) {
            card_code(p->hand[i], code);
            if (strcasecmp(code, args[2]) == 0) break;
        }
        if (i == p->count) { error(s, c, "You do not have that card"); return COMMAND_KEEP_CLIENT; }
        --p->count;
        memmove(&p->hand[i], &p->hand[i+1U], (p->count - i) * sizeof(p->hand[0]));
        if (is_play) ++t->played; else ++t->discarded;
        (void)snprintf(message, sizeof(message), is_play ? "%s played %s" : "%s discarded 1 card",
                       c->nick, code);
        broadcast(s, channel, is_play ? "PLAY" : "DISCARD", message);
        return COMMAND_KEEP_CLIENT;
    }
    if ((strcasecmp(args[0], "LEFT") == 0 || strcasecmp(args[0], "STATUS") == 0) && argc == 2U) {
        Client *dealer = server_find_client_by_id(s, t->dealer_id);
        (void)snprintf(message, sizeof(message),
                       "Dealer: %s; deck: %u remaining; played: %u; discarded: %u; players: %u",
                       dealer != NULL && dealer->registered ? dealer->nick : "(offline)",
                       t->total - t->next, t->played, t->discarded, t->player_count);
        reply(s, c, channel->name, "STATUS", message);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "SHUFFLE") == 0 && argc == 2U) {
        unsigned int decks;
        if (!is_dealer(s, c, t)) return COMMAND_KEEP_CLIENT;
        decks = t->total / 52U;
        shuffle(t, decks);
        (void)snprintf(message, sizeof(message), "%s reset all hands and shuffled %u cards", c->nick, t->total);
        broadcast(s, channel, "SHUFFLE", message);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "END") == 0 && argc == 2U) {
        if (!is_dealer(s, c, t)) return COMMAND_KEEP_CLIENT;
        (void)snprintf(message, sizeof(message), "%s closed the card table", c->nick);
        broadcast(s, channel, "END", message);
        memset(t, 0, sizeof(*t));
        return COMMAND_KEEP_CLIENT;
    }
    error(s, c, "Unknown CARDS subcommand or invalid arguments");
    return COMMAND_KEEP_CLIENT;
}
