/**
 * @file kick.c
 * @brief Implementation of IRC KICK command.
 */

#include "commands.h"
#include "config.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_kick(Server *server, Client *client, char *params) {
    char *channel_name;
    char *nick;
    char *reason;
    Channel *channel;
    Client *target;
    ChannelMember *actor_member;
    ChannelMember *target_member;
    unsigned int actor_rank;
    unsigned int target_rank;
    char message[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "KICK");
        return COMMAND_KEEP_CLIENT;
    }

    channel_name = strtok(params, " ");
    nick = strtok(NULL, " ");
    reason = strtok(NULL, "");
    if (channel_name == NULL || nick == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "KICK");
        return COMMAND_KEEP_CLIENT;
    }
    if (!channel_name_valid(channel_name)) {
        client_sendf(client, ERR_NOSUCHCHANNEL,
                     server->config.server_name, client->nick, channel_name);
        return COMMAND_KEEP_CLIENT;
    }
    if (reason != NULL && *reason == ':') ++reason;
    if (reason == NULL || *reason == '\0') reason = IRC_DEFAULT_KICK_REASON;

    channel = hash_get(&server->channels_by_name, channel_name);
    if (channel == NULL) {
        client_sendf(client, ERR_NOSUCHCHANNEL,
                     server->config.server_name, client->nick, channel_name);
        return COMMAND_KEEP_CLIENT;
    }

    actor_member = channel_find_member(channel, client);
    if (actor_member == NULL) {
        client_sendf(client, ERR_NOTONCHANNEL,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    actor_rank = channel_privilege_rank(actor_member->privileges);
    if (actor_rank < 2U) {
        client_sendf(client, ERR_CHANOPRIVSNEEDED,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK,
                     server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }

    target_member = channel_find_member(channel, target);
    if (target_member == NULL) {
        client_sendf(client, ERR_USERNOTINCHANNEL,
                     server->config.server_name, client->nick,
                     target->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    target_rank = channel_privilege_rank(target_member->privileges);
    if (target != client &&
        channel_privilege_has(target_member->privileges, CHANNEL_PRIV_PROTECTED) &&
        !channel_privilege_has(target_member->privileges, CHANNEL_PRIV_OWNER)) {
        if (!channel_privilege_has(actor_member->privileges,
                                   CHANNEL_PRIV_PROTECTED | CHANNEL_PRIV_OWNER)) {
            client_sendf(client, ERR_ATTACKDENY,
                         server->config.server_name, client->nick,
                         channel->name, target->nick);
            return COMMAND_KEEP_CLIENT;
        }
    } else if (target != client && target_rank >= actor_rank) {
        client_sendf(client, ERR_ATTACKDENY,
                     server->config.server_name, client->nick,
                     channel->name, target->nick);
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(message, sizeof(message),
                   ":%s!%s@%s KICK %s %s :%s\r\n",
                   client->nick, client->user, client->display_host,
                   channel->name, target->nick, reason);
    channel_broadcast(channel, NULL, message);

    channel_remove_client(channel, target);
    server_remove_channel_if_empty(server, channel);
    return COMMAND_KEEP_CLIENT;
}
