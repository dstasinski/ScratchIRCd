/**
 * @file nick.c
 * @brief Implementation of the IRC NICK command.
 *
 * NICK establishes or changes a client's nickname.  Nicknames are validated
 * against the deliberately small syntax supported by this project and are
 * indexed in Server.clients_by_nick, whose hash table performs
 * case-insensitive lookup.  Nick changes update the index before announcing
 * the new nickname to joined channels.
 *
 * Numeric errors and registration replies are supplied by numerics.h through
 * the shared command helpers.
 */

#include "commands.h"
#include "config.h"
#include "numerics.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/** Return non-zero when ch is accepted after the first nickname character. */
static int valid_nick_char(unsigned char ch) {
    return isalnum(ch) || ch == '-' || ch == '_' || ch == '[' || ch == ']' ||
           ch == '\\' || ch == '`' || ch == '^' || ch == '{' || ch == '}';
}

/** Validate nickname length, first-character rules, and remaining characters. */
static int valid_nickname(const char *nick) {
    size_t index;
    size_t length;

    if (nick == NULL) {
        return 0;
    }

    length = strlen(nick);
    if (length == 0U || length > IRC_NICK_MAX) {
        return 0;
    }

    if (!(isalpha((unsigned char)nick[0]) ||
          strchr("[]\\`_^{|}", nick[0]) != NULL)) {
        return 0;
    }

    for (index = 1U; index < length; ++index) {
        if (!valid_nick_char((unsigned char)nick[index])) {
            return 0;
        }
    }
    return 1;
}

/** Announce a registered nickname change to every joined channel. */
static void broadcast_nick_change(Client *client, const char *old_nick) {
    char message[IRCD_MESSAGE_BUFFER_SIZE];
    ClientChannelLink *link;

    snprintf(message, sizeof(message), ":%s!%s@%s NICK :%s\r\n",
             old_nick, client->user, client->host, client->nick);

    /* A future iteration can deduplicate recipients who share many channels. */
    for (link = client->channels; link != NULL; link = link->next) {
        channel_broadcast(link->channel, NULL, message);
    }
}

CommandResult command_nick(Server *server, Client *client, char *params) {
    Client *existing;
    char old_nick[IRC_NICK_MAX + 1U];

    if (params == NULL || *params == '\0') {
        client_sendf(client, ERR_NONICKNAMEGIVEN,
                     IRCD_SERVER_NAME, command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }

    /* This iteration accepts one nickname token and ignores later text. */
    params[strcspn(params, " ")] = '\0';

    if (!valid_nickname(params)) {
        client_sendf(client, ERR_ERRONEUSNICKNAME,
                     IRCD_SERVER_NAME, command_reply_nick(client), params);
        return COMMAND_KEEP_CLIENT;
    }

    existing = hash_get(&server->clients_by_nick, params);
    if (existing != NULL && existing != client) {
        client_sendf(client, ERR_NICKNAMEINUSE,
                     IRCD_SERVER_NAME, command_reply_nick(client), params);
        return COMMAND_KEEP_CLIENT;
    }

    snprintf(old_nick, sizeof(old_nick), "%s", client->nick);
    if (client->nick[0] != '\0') {
        (void)hash_remove(&server->clients_by_nick, client->nick);
    }

    snprintf(client->nick, sizeof(client->nick), "%s", params);
    if (hash_set(&server->clients_by_nick, client->nick, client) < 0) {
        client->nick[0] = '\0';
        return COMMAND_KEEP_CLIENT;
    }

    if (client->registered && old_nick[0] != '\0') {
        broadcast_nick_change(client, old_nick);
    }

    command_maybe_register(client);
    return COMMAND_KEEP_CLIENT;
}
