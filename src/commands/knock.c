/**
 * @file knock.c
 * @brief Implementation of IRC KNOCK.
 *
 * KNOCK asks channel staff for an invitation. It never creates an invite
 * record itself; a halfop or higher must still issue INVITE explicitly.
 * Channel mode +K disables KNOCK for that channel.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_knock(Server *server, Client *client, char *params) {
    char *channel_name;
    char *reason;
    Channel *channel;
    ChannelMember *member;
    int delivered = 0;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "KNOCK");
        return COMMAND_KEEP_CLIENT;
    }

    channel_name = strtok(params, " ");
    reason = strtok(NULL, "");
    if (reason != NULL && *reason == ':') ++reason;
    if (channel_name == NULL || *channel_name == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "KNOCK");
        return COMMAND_KEEP_CLIENT;
    }

    channel = hash_get(&server->channels_by_name, channel_name);
    if (channel == NULL) {
        client_sendf(client, ERR_NOSUCHCHANNEL,
                     server->config.server_name, client->nick, channel_name);
        return COMMAND_KEEP_CLIENT;
    }

    if (channel_has_client(channel, client)) {
        client_sendf(client, ERR_KNOCKONCHAN,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    if (channel_mode_has(channel->modes, CHANNEL_MODE_NO_KNOCK)) {
        client_sendf(client, ERR_CANNOTKNOCK,
                     server->config.server_name, client->nick,
                     channel->name, "channel mode +K");
        return COMMAND_KEEP_CLIENT;
    }

    /* If no access restriction is present, the requester can simply JOIN. */
    if (!channel_mode_has(channel->modes, CHANNEL_MODE_INVITE_ONLY) &&
        channel->key[0] == '\0' &&
        channel->user_limit == 0U) {
        client_sendf(client, ERR_CHANOPEN,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    if (reason == NULL || *reason == '\0') reason = "Knock";

    for (member = channel->members; member != NULL; member = member->next) {
        if (!channel_privilege_has(member->privileges,
                                   CHANNEL_PRIV_HALFOP |
                                   CHANNEL_PRIV_OPERATOR |
                                   CHANNEL_PRIV_PROTECTED |
                                   CHANNEL_PRIV_OWNER)) {
            continue;
        }
        client_sendf(member->client, RPL_KNOCK,
                     server->config.server_name, member->client->nick,
                     channel->name, client->nick, client->user,
                     client->display_host, reason);
        delivered = 1;
    }

    if (delivered) {
        client_sendf(client, RPL_KNOCKDLVR,
                     server->config.server_name, client->nick, channel->name);
    } else {
        client_sendf(client, ERR_CANNOTKNOCK,
                     server->config.server_name, client->nick,
                     channel->name, "no channel staff are available");
    }
    return COMMAND_KEEP_CLIENT;
}
