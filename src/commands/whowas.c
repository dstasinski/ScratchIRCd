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

    if (matched == 0U)
        client_sendf(client, ERR_WASNOSUCHNICK, server->config.server_name,
                     client->nick, nick);
    client_sendf(client, RPL_ENDOFWHOWAS, server->config.server_name,
                 client->nick, nick);
    return COMMAND_KEEP_CLIENT;
}
