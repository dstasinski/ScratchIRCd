/**
 * @file sapart.c
 * @brief Server-authority channel removal for operators with can_override.
 */

#include "commands.h"
#include "channel_log.h"
#include "config.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

CommandResult command_sapart(Server *server, Client *client, char *params) {
    char *nick;
    char *channels;
    char *name;
    Client *target;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_OVERRIDE)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (nick = strtok(params, " ")) == NULL ||
        (channels = strtok(NULL, "")) == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "SAPART");
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK, server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }

    for (name = strtok(channels, ", "); name != NULL; name = strtok(NULL, ", ")) {
        Channel *channel = hash_get(&server->channels_by_name, name);
        char message[IRCD_MESSAGE_BUFFER_SIZE];
        char reason[IRCD_MESSAGE_BUFFER_SIZE];
        if (channel == NULL || !channel_has_client(channel, target)) continue;
        (void)snprintf(reason, sizeof(reason), "Forced part by %s", client->nick);
        (void)snprintf(message, sizeof(message),
                       ":%s!%s@%s PART %s :%s\r\n",
                       target->nick, target->user, target->display_host,
                       channel->name, reason);
        channel_broadcast(channel, NULL, message);
        channel_log_part(server, channel, target, reason);
        snotice_broadcast(server, SNOTICE_MODERATION,
                          "SAPART by %s: %s <- %s [real_ip=%s]",
                          client->nick, target->nick, channel->name, target->real_ip);
        channel_remove_client(channel, target);
        server_remove_channel_if_empty(server, channel);
    }

    client_sendf(client, ":%s NOTICE %s :SAPART completed for %s",
                 server->config.server_name, client->nick, target->nick);
    return COMMAND_KEEP_CLIENT;
}
