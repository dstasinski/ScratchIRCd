/**
 * @file privmsg.c
 * @brief Implementation of IRC PRIVMSG.
 *
 * PRIVMSG supports one target. Channel delivery enforces +n, +m and +M.
 * Direct delivery enforces recipient +R/+T and returns RPL_AWAY when the
 * destination has an active AWAY message.
 */

#include "commands.h"
#include "config.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

CommandResult command_privmsg(Server *server, Client *client, char *params) {
    char *target;
    char *text;
    char message[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NORECIPIENT, server->config.server_name,
                     client->nick, "PRIVMSG");
        return COMMAND_KEEP_CLIENT;
    }

    target = strtok(params, " ");
    text = strtok(NULL, "");
    if (target == NULL || *target == '\0') {
        client_sendf(client, ERR_NORECIPIENT, server->config.server_name,
                     client->nick, "PRIVMSG");
        return COMMAND_KEEP_CLIENT;
    }
    if (text == NULL || *text == '\0' || (text[0] == ':' && text[1] == '\0')) {
        client_sendf(client, ERR_NOTEXTTOSEND, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (*text == ':') ++text;

    (void)snprintf(message, sizeof(message), ":%s!%s@%s PRIVMSG %s :%s\r\n",
                   client->nick, client->user, client->host, target, text);

    if (strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL) {
        Channel *channel = hash_get(&server->channels_by_name, target);
        ChannelMember *member;
        if (channel == NULL) {
            client_sendf(client, ERR_NOSUCHCHANNEL, server->config.server_name,
                         client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }
        member = channel_find_member(channel, client);
        if (channel_mode_has(channel->modes, CHANNEL_MODE_NO_EXTERNAL) && member == NULL) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN, server->config.server_name,
                         client->nick, channel->name, "no external messages (+n)");
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_REGONLY_SPEAK) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED)) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN, server->config.server_name,
                         client->nick, channel->name, "registered nickname required (+M)");
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_MODERATED) &&
            (member == NULL || !channel_privilege_has(member->privileges,
             CHANNEL_PRIV_VOICE | CHANNEL_PRIV_HALFOP |
             CHANNEL_PRIV_OPERATOR | CHANNEL_PRIV_OWNER))) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN, server->config.server_name,
                         client->nick, channel->name, "moderated channel (+m)");
            return COMMAND_KEEP_CLIENT;
        }
        channel_broadcast(channel, client, message);
    } else {
        Client *destination = hash_get(&server->clients_by_nick, target);
        if (destination == NULL) {
            client_sendf(client, ERR_NOSUCHNICK, server->config.server_name,
                         client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }
        if (client_mode_has(destination->modes, CLIENT_MODE_REGONLY_MSG) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED)) {
            client_sendf(client, ERR_NONONREG, server->config.server_name,
                         client->nick, destination->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (client_mode_has(destination->modes, CLIENT_MODE_NO_CTCP) && text[0] == '\001') {
            client_sendf(client, ERR_NOCTCP, server->config.server_name,
                         client->nick, destination->nick);
            return COMMAND_KEEP_CLIENT;
        }
        (void)send(destination->fd, message, strlen(message), MSG_NOSIGNAL);
        if (destination->away[0] != '\0') {
            client_sendf(client, RPL_AWAY, server->config.server_name,
                         client->nick, destination->nick, destination->away);
        }
    }
    return COMMAND_KEEP_CLIENT;
}
