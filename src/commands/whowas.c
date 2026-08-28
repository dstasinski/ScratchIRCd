/**
 * @file whowas.c
 * @brief Query the server-local WHOWAS ring.
 *
 * Syntax:
 *   WHOWAS <nick> [count]
 */

#include "commands.h"
#include "numerics.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void send_whowas_user(Server *server, Client *client,
                             const WhowasRecord *record) {
    char realname[IRC_REALNAME_MAX + 1U];
    int overhead;
    size_t limit;
    size_t length;
    if (server == NULL || client == NULL || record == NULL) return;
    overhead = snprintf(NULL, 0, RPL_WHOWASUSER, server->config.server_name,
                        client->nick, record->nick, record->user,
                        record->host, "");
    if (overhead < 0 || (size_t)overhead > IRC_LINE_CONTENT_MAX) return;
    limit = IRC_LINE_CONTENT_MAX - (size_t)overhead;
    if (limit > IRC_REALNAME_MAX) limit = IRC_REALNAME_MAX;
    length = strlen(record->realname);
    if (length > limit) length = limit;
    memcpy(realname, record->realname, length);
    realname[length] = '\0';
    client_sendf(client, RPL_WHOWASUSER, server->config.server_name,
                 client->nick, record->nick, record->user,
                 record->host, realname);
}

static size_t whowas_query_length(Server *server, Client *client,
                                  const char *query, int include_missing) {
    int end_base;
    int missing_base = 0;
    size_t base;
    size_t length;
    if (server == NULL || client == NULL || query == NULL) return 0U;
    end_base = snprintf(NULL, 0, ":%s 369 %s  :End of WHOWAS",
                        server->config.server_name, client->nick);
    if (include_missing)
        missing_base = snprintf(NULL, 0, ":%s 406 %s  :There was no such nickname",
                                server->config.server_name, client->nick);
    if (end_base < 0 || (include_missing && missing_base < 0)) return 0U;
    base = (size_t)end_base;
    if (include_missing && (size_t)missing_base > base) base = (size_t)missing_base;
    if (base > IRC_LINE_CONTENT_MAX) return 0U;
    length = strlen(query);
    if (length > IRC_LINE_CONTENT_MAX - base) length = IRC_LINE_CONTENT_MAX - base;
    return length;
}

CommandResult command_whowas(Server *server, Client *client, char *params) {
    char *nick;
    char *count_text;
    size_t wanted = 1U;
    size_t matched = 0U;
    size_t scanned;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NONICKNAMEGIVEN,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    nick = strtok(params, " ");
    count_text = strtok(NULL, " ");
    if (nick == NULL || *nick == '\0') {
        client_sendf(client, ERR_NONICKNAMEGIVEN,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (count_text != NULL) {
        char *end = NULL;
        unsigned long parsed;
        errno = 0;
        parsed = strtoul(count_text, &end, 10);
        if (errno == 0 && end != count_text && *end == '\0' && parsed > 0UL) {
            if (parsed > IRCD_WHOWAS_MAX) parsed = IRCD_WHOWAS_MAX;
            wanted = (size_t)parsed;
        }
    }

    for (scanned = 0U; scanned < server->whowas_count && matched < wanted; ++scanned) {
        size_t index = (server->whowas_next + IRCD_WHOWAS_MAX - 1U - scanned) % IRCD_WHOWAS_MAX;
        const WhowasRecord *record = &server->whowas[index];
        char when[64];
        struct tm tm_value;
        if (strcasecmp(record->nick, nick) != 0) continue;
        send_whowas_user(server, client, record);
        when[0] = '\0';
        if (localtime_r(&record->when, &tm_value) != NULL)
            (void)strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S %Z", &tm_value);
        client_sendf(client, RPL_WHOISSERVER, server->config.server_name,
                     client->nick, record->nick, record->server_name,
                     when[0] != '\0' ? when : "historical connection");
        ++matched;
    }

    {
        size_t query_length = whowas_query_length(server, client, nick, matched == 0U);
        if (matched == 0U)
            client_sendf(client, ":%s 406 %s %.*s :There was no such nickname",
                         server->config.server_name, client->nick,
                         (int)query_length, nick);
        client_sendf(client, ":%s 369 %s %.*s :End of WHOWAS",
                     server->config.server_name, client->nick,
                     (int)query_length, nick);
    }
    return COMMAND_KEEP_CLIENT;
}
