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
#include <strings.h>
#include <time.h>

#define CARD_TABLE_LIMIT 32U
#define CARD_DECK_LIMIT 6U
#define CARD_TOTAL_MAX (54U * CARD_DECK_LIMIT)
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
    unsigned int total, next, played, discarded, burned;
    unsigned int deck_count, jokers_per_deck;
    unsigned short discard_cards[CARD_TOTAL_MAX];
    unsigned short public_cards[CARD_TOTAL_MAX];
    unsigned int public_count;
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
    if ((card % (52U + 2U)) >= 52U) { out[0] = 'J'; out[1] = 'K'; out[2] = '\0'; return; }
    out[0] = ranks[(card % 54U) % 13U];
    out[1] = suits[(card % 54U) / 13U];
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
    unsigned int stride = 52U + t->jokers_per_deck;
    t->deck_count = decks;
    t->total = decks * stride;
    t->next = t->played = t->discarded = t->burned = t->player_count = t->public_count = 0U;
    memset(t->players, 0, sizeof(t->players));
    for (i = 0; i < t->total; ++i)
        t->deck[i] = (unsigned short)((i / stride) * 54U + (i % stride));
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
static int remove_from_hand(CardPlayer *p, const char *wanted, unsigned short *card) {
    unsigned int i;
    char code[3];
    if (p == NULL || wanted == NULL || strlen(wanted) != 2U) return 0;
    for (i = 0U; i < p->count; ++i) {
        card_code(p->hand[i], code);
        if (strcasecmp(code, wanted) == 0) {
            *card = p->hand[i];
            --p->count;
            memmove(&p->hand[i], &p->hand[i + 1U],
                    (p->count - i) * sizeof(p->hand[0]));
            return 1;
        }
    }
    return 0;
}
static void public_send(Server *s, Client *c, CardTable *t) {
    char line[300];
    size_t used = 0U;
    unsigned int i;
    if (t->public_count == 0U) {
        reply(s, c, t->channel, "TABLE", "(empty)");
        return;
    }
    line[0] = '\0';
    for (i = 0U; i < t->public_count; ++i) {
        char code[3];
        int n;
        card_code(t->public_cards[i], code);
        n = snprintf(line + used, sizeof(line) - used,
                     "%s%s", used ? " " : "", code);
        if (n < 0 || (size_t)n >= sizeof(line) - used) {
            reply(s, c, t->channel, "TABLE", line);
            used = 0U;
            n = snprintf(line, sizeof(line), "%s", code);
        }
        used += (size_t)n;
    }
    if (used) reply(s, c, t->channel, "TABLE", line);
}
static void hand_send(Server *s, Client *c, CardTable *t, CardPlayer *p) {
    char message[1024];
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
    if (argc < 2U) { error(s, c, "Usage: CARDS <NEW|REQUEST|ASK|DEAL|SHOW|BURN|GIVE|REVEAL|TABLE|HAND|PLAY|DISCARD|DISCARDS|TAKE|MELD|LEFT|STATUS|SHUFFLE|END> #channel ..."); return COMMAND_KEEP_CLIENT; }
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
        int jokers = argc >= 4U && strcasecmp(args[3], "JOKERS") == 0;
        if (argc > 4U || (argc == 4U && !jokers) ||
            (argc >= 3U && !parse_count(args[2], CARD_DECK_LIMIT, &decks))) {
            error(s, c, "Usage: CARDS NEW #channel [decks:1-6] [JOKERS]"); return COMMAND_KEEP_CLIENT;
        }
        if (t != NULL) { error(s, c, "A deck already exists; use END first"); return COMMAND_KEEP_CLIENT; }
        for (i = 0U; i < CARD_TABLE_LIMIT && tables[i].active; ++i) {}
        if (i == CARD_TABLE_LIMIT) { error(s, c, "Too many active card tables"); return COMMAND_KEEP_CLIENT; }
        t = &tables[i];
        memset(t, 0, sizeof(*t));
        t->active = 1;
        t->dealer_id = c->id;
        t->jokers_per_deck = jokers ? 2U : 0U;
        (void)snprintf(t->channel, sizeof(t->channel), "%s", channel->name);
        shuffle(t, decks);
        (void)snprintf(message, sizeof(message), "%s opened a %u-deck table (%u cards)",
                       c->nick, decks, t->total);
        broadcast(s, channel, "NEW", message);
        return COMMAND_KEEP_CLIENT;
    }
    if (t == NULL) { error(s, c, "No active card table in this channel"); return COMMAND_KEEP_CLIENT; }
    if (strcasecmp(args[0], "REQUEST") == 0) {
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
    if (strcasecmp(args[0], "ASK") == 0) {
        Client *target;
        const char *rank;
        if (argc != 4U || strlen(args[3]) != 1U ||
            strchr("A23456789TJQK", args[3][0]) == NULL) {
            error(s, c, "Usage: CARDS ASK #channel <nick> <rank:A,2-9,T,J,Q,K>");
            return COMMAND_KEEP_CLIENT;
        }
        target = hash_get(&s->clients_by_nick, args[2]);
        if (target == NULL || !target->registered || !has_room(channel, target)) {
            error(s, c, "Target must be an online member of this channel");
            return COMMAND_KEEP_CLIENT;
        }
        rank = args[3];
        (void)snprintf(message, sizeof(message), "%s asks %s for rank %s",
                       c->nick, target->nick, rank);
        broadcast(s, channel, "ASK", message);
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
    if ((strcasecmp(args[0], "SHOW") == 0 ||
         strcasecmp(args[0], "BURN") == 0) && argc == 3U) {
        int visible = strcasecmp(args[0], "SHOW") == 0;
        if (!is_dealer(s, c, t)) return COMMAND_KEEP_CLIENT;
        if (!parse_count(args[2], CARD_TOTAL_MAX, &n) || n > t->total - t->next) {
            error(s, c, "Invalid count or insufficient cards remaining");
            return COMMAND_KEEP_CLIENT;
        }
        for (i = 0U; i < n; ++i) {
            unsigned short card = t->deck[t->next++];
            if (visible) t->public_cards[t->public_count++] = card;
            else ++t->burned;
            if (visible) {
                char code[3];
                card_code(card, code);
                broadcast(s, channel, "SHOW", code);
            }
        }
        if (!visible) {
            (void)snprintf(message, sizeof(message),
                           "%s burned %u card(s) (%u remaining)",
                           c->nick, n, t->total - t->next);
            broadcast(s, channel, "BURN", message);
        }
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "TABLE") == 0 && argc == 2U) {
        public_send(s, c, t);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "GIVE") == 0 && argc == 4U) {
        Client *target = hash_get(&s->clients_by_nick, args[2]);
        CardPlayer *recipient;
        unsigned short card;
        if (target == NULL || !target->registered || !has_room(channel, target)) {
            error(s, c, "Recipient must be an online channel member");
            return COMMAND_KEEP_CLIENT;
        }
        p = player_find(t, c->id, 0);
        recipient = player_find(t, target->id, 0);
        if (target->id == c->id ||
            (recipient == NULL && t->player_count >= CARD_PLAYER_LIMIT) ||
            (recipient != NULL && recipient->count >= CARD_TOTAL_MAX)) {
            error(s, c, "Invalid recipient or hand capacity reached");
            return COMMAND_KEEP_CLIENT;
        }
        if (!remove_from_hand(p, args[3], &card)) {
            error(s, c, "You do not have that card");
            return COMMAND_KEEP_CLIENT;
        }
        recipient = player_find(t, target->id, 1);
        recipient->hand[recipient->count++] = card;
        reply(s, target, channel->name, "GIVE", args[3]);
        (void)snprintf(message, sizeof(message), "%s gave 1 card to %s",
                       c->nick, target->nick);
        broadcast(s, channel, "GIVE", message);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "REVEAL") == 0 && argc == 2U) {
        char line[320];
        size_t used = 0U;
        p = player_find(t, c->id, 0);
        if (p == NULL || p->count == 0U) {
            error(s, c, "Your hand is empty");
            return COMMAND_KEEP_CLIENT;
        }
        line[0] = '\0';
        for (i = 0U; i < p->count; ++i) {
            char code[3];
            int written;
            card_code(p->hand[i], code);
            written = snprintf(line + used, sizeof(line) - used,
                               "%s%s", used ? " " : "", code);
            if (written < 0 || (size_t)written >= sizeof(line) - used) {
                broadcast(s, channel, "REVEAL", line);
                used = 0U;
                written = snprintf(line, sizeof(line), "%s", code);
            }
            used += (size_t)written;
        }
        if (used) broadcast(s, channel, "REVEAL", line);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "MELD") == 0 && argc >= 4U) {
        CardPlayer *hand = player_find(t, c->id, 0);
        CardPlayer copy;
        unsigned short selected[6];
        char codes[64];
        size_t used = 0U;
        if (hand == NULL || hand->count < argc - 2U ||
            t->public_count + argc - 2U > CARD_TOTAL_MAX) {
            error(s, c, "Insufficient cards or public table capacity");
            return COMMAND_KEEP_CLIENT;
        }
        copy = *hand;
        for (i = 2U; i < argc; ++i) {
            if (!remove_from_hand(&copy, args[i], &selected[i - 2U])) {
                error(s, c, "Meld contains a card not in your hand");
                return COMMAND_KEEP_CLIENT;
            }
        }
        *hand = copy;
        codes[0] = '\0';
        for (i = 2U; i < argc; ++i) {
            char card[3];
            int written;
            card_code(selected[i - 2U], card);
            t->public_cards[t->public_count++] = selected[i - 2U];
            written = snprintf(codes + used, sizeof(codes) - used,
                               "%s%s", used ? " " : "", card);
            if (written < 0 || (size_t)written >= sizeof(codes) - used) break;
            used += (size_t)written;
        }
        t->played += argc - 2U;
        (void)snprintf(message, sizeof(message), "%s melded %s", c->nick, codes);
        broadcast(s, channel, "MELD", message);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "TAKE") == 0 && argc == 2U) {
        unsigned short card;
        char code[3];
        if (t->discarded == 0U) {
            error(s, c, "Discard pile is empty");
            return COMMAND_KEEP_CLIENT;
        }
        p = player_find(t, c->id, 0);
        if ((p == NULL && t->player_count == CARD_PLAYER_LIMIT) ||
            (p != NULL && p->count == CARD_TOTAL_MAX)) {
            error(s, c, "Hand capacity reached");
            return COMMAND_KEEP_CLIENT;
        }
        card = t->discard_cards[--t->discarded];
        p = player_find(t, c->id, 1);
        p->hand[p->count++] = card;
        card_code(card, code);
        reply(s, c, channel->name, "TAKE", code);
        (void)snprintf(message, sizeof(message),
                       "%s took the top discard (%u discarded remain)",
                       c->nick, t->discarded);
        broadcast(s, channel, "TAKE", message);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "DISCARDS") == 0 && argc == 2U) {
        char code[3];
        if (t->discarded == 0U) reply(s, c, channel->name, "DISCARDS", "(empty)");
        else {
            card_code(t->discard_cards[t->discarded - 1U], code);
            (void)snprintf(message, sizeof(message), "Top: %s; count: %u",
                           code, t->discarded);
            reply(s, c, channel->name, "DISCARDS", message);
        }
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
        { unsigned short card = p->hand[i];
          --p->count;
          memmove(&p->hand[i], &p->hand[i+1U], (p->count - i) * sizeof(p->hand[0]));
          if (is_play) t->public_cards[t->public_count++] = card;
          else t->discard_cards[t->discarded++] = card;
        }
        if (is_play) ++t->played;
        if (is_play) (void)snprintf(message, sizeof(message), "%s played %s", c->nick, code);
        else (void)snprintf(message, sizeof(message), "%s discarded 1 card", c->nick);
        broadcast(s, channel, is_play ? "PLAY" : "DISCARD", message);
        return COMMAND_KEEP_CLIENT;
    }
    if ((strcasecmp(args[0], "LEFT") == 0 || strcasecmp(args[0], "STATUS") == 0) && argc == 2U) {
        Client *dealer = server_find_client_by_id(s, t->dealer_id);
        (void)snprintf(message, sizeof(message),
                       "Dealer: %s; deck: %u remaining; played: %u; discarded: %u; burned: %u; public: %u; players: %u",
                       dealer != NULL && dealer->registered ? dealer->nick : "(offline)",
                       t->total - t->next, t->played, t->discarded, t->burned, t->public_count, t->player_count);
        reply(s, c, channel->name, "STATUS", message);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(args[0], "SHUFFLE") == 0 && argc == 2U) {
        unsigned int decks;
        if (!is_dealer(s, c, t)) return COMMAND_KEEP_CLIENT;
        decks = t->deck_count;
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
