/**
 * @file presence.c
 * @brief Runtime SILENCE, WATCH, and WHOWAS helpers.
 */

#include "presence.h"
#include "channel_policy.h"
#include "numerics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ScratchIRCd is deliberately single-server; this points at that live Server. */
static Server *active_server = NULL;

static unsigned char rfc1459_fold(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return (unsigned char)(ch + ('a' - 'A'));
    switch (ch) {
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case '^': return '~';
        default: return ch;
    }
}

static int rfc1459_equal(const char *left, const char *right) {
    if (left == NULL || right == NULL) return 0;
    while (*left != '\0' && *right != '\0') {
        if (rfc1459_fold((unsigned char)*left) !=
            rfc1459_fold((unsigned char)*right)) return 0;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static void presence_on_client_free(Client *client) {
    if (active_server == NULL || client == NULL || !client->registered) return;
    presence_whowas_record(active_server, client, NULL);
    presence_watch_offline(active_server, client, NULL);
}

void presence_reset_runtime_state(void) {
    active_server = NULL;
    client_set_free_hook(NULL);
}

int presence_silence_add(Client *client, const char *mask) {
    ClientSilenceEntry *entry;
    if (client == NULL || mask == NULL || *mask == '\0') return -1;
    for (entry = client->silence_list; entry != NULL; entry = entry->next)
        if (rfc1459_equal(entry->mask, mask)) return 0;
    if (client->silence_count >= IRCD_SILENCE_MAX) return -1;
    entry = calloc(1U, sizeof(*entry));
    if (entry == NULL) return -1;
    (void)snprintf(entry->mask, sizeof(entry->mask), "%s", mask);
    entry->next = client->silence_list;
    client->silence_list = entry;
    ++client->silence_count;
    return 1;
}

int presence_silence_remove(Client *client, const char *mask) {
    ClientSilenceEntry **link;
    if (client == NULL || mask == NULL || *mask == '\0') return -1;
    for (link = &client->silence_list; *link != NULL; link = &(*link)->next) {
        if (rfc1459_equal((*link)->mask, mask)) {
            ClientSilenceEntry *victim = *link;
            *link = victim->next;
            free(victim);
            --client->silence_count;
            return 1;
        }
    }
    return 0;
}

int presence_silence_matches(const Client *recipient, const Client *sender) {
    ClientSilenceEntry *entry;
    char identity[IRCD_MESSAGE_BUFFER_SIZE];
    if (recipient == NULL || sender == NULL) return 0;
    (void)snprintf(identity, sizeof(identity), "%s!%s@%s",
                   sender->nick, sender->user, sender->display_host);
    for (entry = recipient->silence_list; entry != NULL; entry = entry->next)
        if (irc_mask_match(entry->mask, identity)) return 1;
    return 0;
}

int presence_watch_contains(const Client *client, const char *nick) {
    ClientWatchEntry *entry;
    if (client == NULL || nick == NULL) return 0;
    for (entry = client->watch_list; entry != NULL; entry = entry->next)
        if (rfc1459_equal(entry->nick, nick)) return 1;
    return 0;
}

int presence_watch_add(Client *client, const char *nick) {
    ClientWatchEntry *entry;
    if (client == NULL || nick == NULL || *nick == '\0') return -1;
    if (presence_watch_contains(client, nick)) return 0;
    if (client->watch_count >= IRCD_WATCH_MAX) return -1;
    entry = calloc(1U, sizeof(*entry));
    if (entry == NULL) return -1;
    (void)snprintf(entry->nick, sizeof(entry->nick), "%s", nick);
    entry->next = client->watch_list;
    client->watch_list = entry;
    ++client->watch_count;
    return 1;
}

int presence_watch_remove(Client *client, const char *nick) {
    ClientWatchEntry **link;
    if (client == NULL || nick == NULL || *nick == '\0') return -1;
    for (link = &client->watch_list; *link != NULL; link = &(*link)->next) {
        if (rfc1459_equal((*link)->nick, nick)) {
            ClientWatchEntry *victim = *link;
            *link = victim->next;
            free(victim);
            --client->watch_count;
            return 1;
        }
    }
    return 0;
}

void presence_watch_online(Server *server, const Client *subject) {
    size_t i;
    if (server == NULL || subject == NULL || !subject->registered || subject->nick[0] == '\0') return;
    active_server = server;
    client_set_free_hook(presence_on_client_free);
    for (i = 0U; i < server->client_count; ++i) {
        Client *watcher = server->clients[i];
        if (watcher == NULL || !watcher->registered || !presence_watch_contains(watcher, subject->nick)) continue;
        client_sendf(watcher, RPL_LOGON, server->config.server_name, watcher->nick,
                     subject->nick, subject->user, subject->display_host,
                     (int)subject->signon_time);
    }
}

void presence_watch_offline(Server *server, const Client *subject,
                            const char *nick_override) {
    size_t i;
    const char *nick;
    time_t now = time(NULL);
    if (server == NULL || subject == NULL) return;
    nick = nick_override != NULL && *nick_override != '\0' ? nick_override : subject->nick;
    if (*nick == '\0') return;
    for (i = 0U; i < server->client_count; ++i) {
        Client *watcher = server->clients[i];
        if (watcher == NULL || !watcher->registered || !presence_watch_contains(watcher, nick)) continue;
        client_sendf(watcher, RPL_LOGOFF, server->config.server_name, watcher->nick,
                     nick, subject->user, subject->display_host, (int)now);
    }
}

void presence_whowas_record(Server *server, const Client *client,
                            const char *nick_override) {
    WhowasRecord *record;
    const char *nick;
    if (server == NULL || client == NULL || !client->registered) return;
    nick = nick_override != NULL && *nick_override != '\0' ? nick_override : client->nick;
    if (*nick == '\0') return;
    record = &server->whowas[server->whowas_next];
    memset(record, 0, sizeof(*record));
    (void)snprintf(record->nick, sizeof(record->nick), "%s", nick);
    (void)snprintf(record->user, sizeof(record->user), "%s", client->user);
    (void)snprintf(record->host, sizeof(record->host), "%s", client->display_host);
    (void)snprintf(record->realname, sizeof(record->realname), "%s", client->realname);
    (void)snprintf(record->server_name, sizeof(record->server_name), "%s",
                   server->config.server_name);
    record->when = time(NULL);
    server->whowas_next = (server->whowas_next + 1U) % IRCD_WHOWAS_MAX;
    if (server->whowas_count < IRCD_WHOWAS_MAX) ++server->whowas_count;
}

void presence_client_clear(Client *client) {
    if (client == NULL) return;
    while (client->silence_list != NULL) {
        ClientSilenceEntry *next = client->silence_list->next;
        free(client->silence_list);
        client->silence_list = next;
    }
    client->silence_count = 0U;
    while (client->watch_list != NULL) {
        ClientWatchEntry *next = client->watch_list->next;
        free(client->watch_list);
        client->watch_list = next;
    }
    client->watch_count = 0U;
}
