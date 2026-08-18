/**
 * @file privmsg.c
 * @brief Implementation of the IRC PRIVMSG command.
 *
 * PRIVMSG currently supports one target. Targets beginning with '#' or '&'
 * are resolved as channels; all other targets are resolved as nicknames.
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
                     server->config.server_name, client->nick, "PRIVMSG");
        return COMMAND_KEEP_CLIENT;
    }

    target = strtok(params, " ");
    text = strtok(NULL, "");

    if (target == NULL || *target == '\0') {
        client_sendf(client, ERR_NORECIPIENT,
                     server->config.server_name, client->nick, "PRIVMSG");
        return COMMAND_KEEP_CLIENT;
    }

    if (text == NULL || *text == '\0' || (text[0] == ':' && text[1] == '\0')) {
        client_sendf(client, ERR_NOTEXTTOSEND,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    if (*text == ':') {
        ++text;
    }

    (void)snprintf(message, sizeof(message), ":%s!%s@%s PRIVMSG %s :%s\r\n",
                   client->nick, client->user, client->host, target, text);

    if (strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL) {
        Channel *channel = hash_get(&server->channels_by_name, target);

        if (channel == NULL) {
            client_sendf(client, ERR_NOSUCHCHANNEL,
                         server->config.server_name, client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }

        if (!channel_has_client(channel, client)) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN,
                         server->config.server_name, client->nick, channel->name,
                         IRC_CANNOT_SEND_NOT_MEMBER_TEXT);
            return COMMAND_KEEP_CLIENT;
        }

        channel_broadcast(channel, client, message);
    } else {
        Client *destination = hash_get(&server->clients_by_nick, target);

        if (destination == NULL) {
            client_sendf(client, ERR_NOSUCHNICK,
                         server->config.server_name, client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }

        (void)send(destination->fd, message, strlen(message), MSG_NOSIGNAL);
    }

    return COMMAND_KEEP_CLIENT;
}
