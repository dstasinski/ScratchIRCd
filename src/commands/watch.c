/**
 * @file watch.c
 * @brief Client-local nickname WATCH list management.
 *
 * Syntax:
 *   WATCH +nick [-nick ...]
 *   WATCH                  - list watched nicknames
 */

#include "commands.h"
#include "numerics.h"
#include "presence.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int valid_watch_nick_char(unsigned char ch) {
    return isalnum(ch) || ch == '-' || ch == '_' || ch == '[' || ch == ']' ||
           ch == '\\' || ch == '`' || ch == '^' || ch == '{' || ch == '}' ||
           ch == '|';
}

static int valid_watch_nick(const char *nick) {
    size_t index;
    size_t length;
    if (nick == NULL) return 0;
    length = strlen(nick);
    if (length == 0U || length > IRC_NICK_MAX) return 0;
    if (!(isalpha((unsigned char)nick[0]) || strchr("[]\\`_^{|}", nick[0]) != NULL))
        return 0;
    for (index = 1U; index < length; ++index)
        if (!valid_watch_nick_char((unsigned char)nick[index])) return 0;
    return 1;
}

static void report_current(Server *server, Client *client, const char *nick) {
    Client *subject = hash_get(&server->clients_by_nick, nick);
    if (subject != NULL && subject->registered) {
        client_sendf(client, RPL_NOWON, server->config.server_name, client->nick,
                     subject->nick, subject->user, subject->display_host,
                     (long)subject->signon_time);
    } else {
        client_sendf(client, RPL_NOWOFF, server->config.server_name, client->nick,
                     nick, "*", "*", 0L);
    }
}

static int watch_list_payload_fits(Server *server, Client *client,
                                   const char *payload) {
    int written;
    if (server == NULL || client == NULL || payload == NULL) return 0;
    written = snprintf(NULL, 0, RPL_WATCHLIST,
                       server->config.server_name, client->nick, payload);
    return written >= 0 && (size_t)written <= IRC_LINE_CONTENT_MAX;
}

static void send_watch_list_payload(Server *server, Client *client,
                                    const char *payload) {
    if (watch_list_payload_fits(server, client, payload))
        client_sendf(client, RPL_WATCHLIST,
                     server->config.server_name, client->nick, payload);
}

static void list_watch(Server *server, Client *client) {
    ClientWatchEntry *entry;
    char list[IRC_LINE_CONTENT_MAX + 1U] = "";
    size_t used = 0U;

    for (entry = client->watch_list; entry != NULL; entry = entry->next) {
        char candidate[IRC_LINE_CONTENT_MAX + 1U];
        int written = snprintf(candidate, sizeof(candidate), "%s%s%s",
                               list, used != 0U ? " " : "", entry->nick);
        if (written >= 0 && (size_t)written < sizeof(candidate) &&
            watch_list_payload_fits(server, client, candidate)) {
            memcpy(list, candidate, (size_t)written + 1U);
            used = (size_t)written;
        } else {
            if (used != 0U) send_watch_list_payload(server, client, list);
            if (!watch_list_payload_fits(server, client, entry->nick)) continue;
            (void)snprintf(list, sizeof(list), "%s", entry->nick);
            used = strlen(list);
        }
    }
    send_watch_list_payload(server, client, list);
    client_sendf(client, RPL_ENDOFWATCHLIST, server->config.server_name,
                 client->nick, 'L');
}

CommandResult command_watch(Server *server, Client *client, char *params) {
    char *token;
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        list_watch(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    for (token = strtok(params, " "); token != NULL; token = strtok(NULL, " ")) {
        const char *nick;
        if ((token[0] != '+' && token[0] != '-') || token[1] == '\0') continue;
        nick = token + 1;
        if (!valid_watch_nick(nick)) continue;
        if (token[0] == '+') {
            int rc = presence_watch_add(client, nick);
            if (rc < 0) {
                client_sendf(client, ERR_TOOMANYWATCH, server->config.server_name,
                             client->nick, nick);
                break;
            }
            report_current(server, client, nick);
        } else if (presence_watch_remove(client, nick) >= 0) {
            Client *subject = hash_get(&server->clients_by_nick, nick);
            int online = subject != NULL && subject->registered;
            client_sendf(client, RPL_WATCHOFF, server->config.server_name,
                         client->nick, nick,
                         online ? subject->user : "*",
                         online ? subject->display_host : "*",
                         (int)time(NULL));
        }
    }
    return COMMAND_KEEP_CLIENT;
}
