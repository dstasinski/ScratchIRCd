/**
 * @file irc.c
 * @brief Minimal IRC line parser connecting the network layer to commands.
 */

#include "irc.h"
#include "commands.h"
#include "ircv3.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

size_t irc_topic_limit(const Server *server) {
    char nick[IRC_NICK_MAX + 1U];
    char user[IRC_USER_MAX + 1U];
    char host[IRC_HOST_MAX + 1U];
    char channel[IRC_CHANNEL_NAME_MAX + 1U];
    int query_overhead;
    int relay_overhead;
    size_t query_limit;
    size_t relay_limit;
    size_t limit = IRC_CHANNEL_TOPIC_MAX;

    if (server == NULL) return 0U;
    memset(nick, 'n', IRC_NICK_MAX);
    nick[IRC_NICK_MAX] = '\0';
    memset(user, 'u', IRC_USER_MAX);
    user[IRC_USER_MAX] = '\0';
    memset(host, 'h', IRC_HOST_MAX);
    host[IRC_HOST_MAX] = '\0';
    channel[0] = '#';
    memset(channel + 1, 'c', IRC_CHANNEL_NAME_MAX - 1U);
    channel[IRC_CHANNEL_NAME_MAX] = '\0';

    query_overhead = snprintf(NULL, 0, RPL_TOPIC,
                              server->config.server_name, nick, channel, "");
    relay_overhead = snprintf(NULL, 0, ":%s!%s@%s TOPIC %s :",
                              nick, user, host, channel);
    if (query_overhead < 0 || relay_overhead < 0 ||
        (size_t)query_overhead >= IRC_LINE_CONTENT_MAX ||
        (size_t)relay_overhead >= IRC_LINE_CONTENT_MAX)
        return 0U;

    query_limit = IRC_LINE_CONTENT_MAX - (size_t)query_overhead;
    relay_limit = IRC_LINE_CONTENT_MAX - (size_t)relay_overhead;
    if (query_limit < limit) limit = query_limit;
    if (relay_limit < limit) limit = relay_limit;
    return limit;
}

int irc_handle_line(Server *server, Client *client, char *line) {
    char *tags = NULL;
    char *command;
    char *params;
    CommandResult result;

    if (server == NULL || client == NULL || line == NULL) return 0;
    if (line[0] == '@') {
        char *separator = strchr(line, ' ');
        if (separator == NULL) return 0;
        *separator = '\0';
        tags = line + 1;
        line = separator + 1;
        while (*line == ' ') ++line;
    }
    if (line[0] == ':') {
        snotice_broadcast(server, SNOTICE_SECURITY,
                          "Protocol violation: client-supplied prefix from %s [real_ip=%s]",
                          command_reply_nick(client), client->real_ip);
        client_sendf(client, ":%s ERROR :Client-supplied prefixes are not permitted",
                     server->config.server_name);
        return 1;
    }
    command = strtok(line, " ");
    params = strtok(NULL, "");
    if (command == NULL) return 0;
    ircv3_begin_command(server, client, tags);
    result = command_dispatch(server, client, command, params);
    ircv3_end_command(client);
    return result == COMMAND_DISCONNECT_CLIENT ? 1 : 0;
}
