/**
 * @file privmsg.c
 * @brief Implementation of the IRC PRIVMSG command.
 *
 * PRIVMSG sends a message to exactly one target in this iteration.  A target
 * beginning with IRC_CHANNEL_PREFIX is resolved through the server's
 * case-insensitive channel hash table; other targets are resolved through the
 * case-insensitive nickname hash table.  Channel messages are broadcast to all
 * members except the sender.
 */

#include "commands.h"
#include "config.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

CommandResult command_privmsg(Server *server, Client *client, char *params) {
    char *target;
    char *text;
    char message[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL) {
        client_sendf(client, ERR_NORECIPIENT,
                     IRCD_SERVER_NAME, client->nick, "PRIVMSG");
        return COMMAND_KEEP_CLIENT;
    }

    target = strtok(params, " ");
    text = strtok(NULL, "");

    if (target == NULL || *target == '\0') {
        client_sendf(client, ERR_NORECIPIENT,
                     IRCD_SERVER_NAME, client->nick, "PRIVMSG");
        return COMMAND_KEEP_CLIENT;
    }

    if (text == NULL || *text == '\0' || (text[0] == ':' && text[1] == '\0')) {
        client_sendf(client, ERR_NOTEXTTOSEND,
                     IRCD_SERVER_NAME, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    if (*text == ':') {
        ++text;
    }

    snprintf(message, sizeof(message), ":%s!%s@%s PRIVMSG %s :%s\r\n",
             client->nick, client->user, client->host, target, text);

    if (target[0] == IRC_CHANNEL_PREFIX) {
        Channel *channel = hash_get(&server->channels_by_name, target);

        if (channel == NULL) {
            client_sendf(client, ERR_NOSUCHCHANNEL,
                         IRCD_SERVER_NAME, client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }

        if (!channel_has_client(channel, client)) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN,
                         IRCD_SERVER_NAME, client->nick, channel->name,
                         IRC_CANNOT_SEND_NOT_MEMBER_TEXT);
            return COMMAND_KEEP_CLIENT;
        }

        channel_broadcast(channel, client, message);
    } else {
        Client *destination = hash_get(&server->clients_by_nick, target);

        if (destination == NULL) {
            client_sendf(client, ERR_NOSUCHNICK,
                         IRCD_SERVER_NAME, client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }

        (void)send(destination->fd, message, strlen(message), MSG_NOSIGNAL);
    }

    return COMMAND_KEEP_CLIENT;
}
