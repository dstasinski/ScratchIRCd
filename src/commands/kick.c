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

static int kick_wire_fits(const Client *source, const char *channel,
                          const char *target, const char *reason) {
    size_t length;
    if (source == NULL || channel == NULL || target == NULL || reason == NULL) return 0;
    length = 1U + strlen(source->nick) + 1U + strlen(source->user) + 1U +
             strlen(source->display_host) + sizeof(" KICK ") - 1U +
             strlen(channel) + 1U + strlen(target) + sizeof(" :") - 1U +
             strlen(reason);
    return length <= IRC_LINE_CONTENT_MAX;
}

static void send_kick_query_error(Server *server, Client *client,
                                  int numeric, const char *query) {
    const char *text;
    int base_length;
    size_t length;
    if (server == NULL || client == NULL || query == NULL) return;
    text = numeric == 403 ? "No such channel" : "No such nick/channel";
    base_length = snprintf(NULL, 0, ":%s %03d %s  :%s",
                           server->config.server_name, numeric,
                           client->nick, text);
    if (base_length < 0 || (size_t)base_length > IRC_LINE_CONTENT_MAX) return;
    length = strlen(query);
    if (length > IRC_LINE_CONTENT_MAX - (size_t)base_length)
        length = IRC_LINE_CONTENT_MAX - (size_t)base_length;
    client_sendf(client, ":%s %03d %s %.*s :%s",
                 server->config.server_name, numeric, client->nick,
                 (int)length, query, text);
}

CommandResult command_kick(Server *server, Client *client, char *params) {
    char *channel_name;
    char *nick;
    char *reason;
    const char *delivered_reason;
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
        send_kick_query_error(server, client, 403, channel_name);
        return COMMAND_KEEP_CLIENT;
    }
    if (reason != NULL && *reason == ':') ++reason;
    delivered_reason = reason != NULL && *reason != '\0'
                           ? reason : IRC_DEFAULT_KICK_REASON;

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
        send_kick_query_error(server, client, 401, nick);
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

    if (!kick_wire_fits(client, channel->name, target->nick, delivered_reason))
        delivered_reason = IRC_DEFAULT_KICK_REASON;

    (void)snprintf(message, sizeof(message),
                   ":%s!%s@%s KICK %s %s :%s\r\n",
                   client->nick, client->user, client->display_host,
                   channel->name, target->nick, delivered_reason);
    channel_broadcast(channel, NULL, message);

    channel_remove_client(channel, target);
    server_remove_channel_if_empty(server, channel);
    return COMMAND_KEEP_CLIENT;
}
