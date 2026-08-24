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

#include <stdio.h>
#include <string.h>
#include <time.h>

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

static void list_watch(Server *server, Client *client) {
    ClientWatchEntry *entry;
    char list[IRCD_MESSAGE_BUFFER_SIZE];
    size_t used = 0U;
    list[0] = '\0';
    for (entry = client->watch_list; entry != NULL; entry = entry->next) {
        int written = snprintf(list + used, sizeof(list) - used, "%s%s",
                               used != 0U ? " " : "", entry->nick);
        if (written < 0 || (size_t)written >= sizeof(list) - used) break;
        used += (size_t)written;
    }
    client_sendf(client, RPL_WATCHLIST, server->config.server_name,
                 client->nick, list);
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
        if (strlen(nick) > IRC_NICK_MAX) continue;
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
