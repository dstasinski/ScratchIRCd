/**
 * @file notice.c
 * @brief Implementation of the IRC NOTICE command.
 *
 * NOTICE mirrors PRIVMSG delivery restrictions without emitting automatic
 * error replies for delivery failures. All client-visible source prefixes use
 * display_host; real identity never leaks through NOTICE.
 */

#include "commands.h"
#include "config.h"
#include "modes.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static int is_ctcp(const char *text) {
    return text != NULL && text[0] == '\001';
}

CommandResult command_notice(Server *server, Client *client, char *params) {
    char *target;
    char *text;
    char message[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) return COMMAND_KEEP_CLIENT;

    target = strtok(params, " ");
    text = strtok(NULL, "");
    if (target == NULL || text == NULL || *target == '\0') return COMMAND_KEEP_CLIENT;
    if (*text == ':') ++text;

    (void)snprintf(message, sizeof(message),
                   ":%s!%s@%s NOTICE %s :%s\r\n",
                   client->nick, client->user, client->display_host, target, text);

    if (strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL) {
        Channel *channel = hash_get(&server->channels_by_name, target);
        ChannelMember *member;

        if (channel == NULL || channel_mode_has(channel->modes, CHANNEL_MODE_NO_NOTICE))
            return COMMAND_KEEP_CLIENT;

        member = channel_find_member(channel, client);
        if (member == NULL && channel_mode_has(channel->modes, CHANNEL_MODE_NO_EXTERNAL))
            return COMMAND_KEEP_CLIENT;
        if (channel_mode_has(channel->modes, CHANNEL_MODE_REGONLY_SPEAK) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED))
            return COMMAND_KEEP_CLIENT;
        if (channel_mode_has(channel->modes, CHANNEL_MODE_MODERATED) &&
            (member == NULL ||
             !channel_privilege_has(member->privileges,
                                    CHANNEL_PRIV_VOICE |
                                    CHANNEL_PRIV_HALFOP |
                                    CHANNEL_PRIV_OPERATOR |
                                    CHANNEL_PRIV_OWNER))) {
            return COMMAND_KEEP_CLIENT;
        }

        channel_broadcast(channel, client, message);
    } else {
        Client *destination = hash_get(&server->clients_by_nick, target);
        if (destination == NULL) return COMMAND_KEEP_CLIENT;
        if (client_mode_has(destination->modes, CLIENT_MODE_REGONLY_MSG) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED))
            return COMMAND_KEEP_CLIENT;
        if (client_mode_has(destination->modes, CLIENT_MODE_NO_CTCP) && is_ctcp(text))
            return COMMAND_KEEP_CLIENT;
        (void)send(destination->fd, message, strlen(message), MSG_NOSIGNAL);
    }

    return COMMAND_KEEP_CLIENT;
}
