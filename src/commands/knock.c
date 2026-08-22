/**
 * @file knock.c
 * @brief Implementation of IRC KNOCK.
 *
 * KNOCK asks channel staff for an invitation. It never creates an invite
 * record itself; a halfop or higher must still issue INVITE explicitly.
 * Channel mode +K disables KNOCK for that channel. Banned clients cannot use
 * KNOCK as a side channel to contact channel staff.
 */

#include "commands.h"
#include "channel_policy.h"
#include "modes.h"
#include "numerics.h"

#include <string.h>

CommandResult command_knock(Server *server, Client *client, char *params) {
    char *channel_name;
    char *reason;
    Channel *channel;
    ChannelMember *member;
    int delivered = 0;
    int is_full;

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
        client_sendf(client, ERR_CANNOTKNOCK,
                     server->config.server_name, client->nick,
                     channel->name, "you are already on the channel");
        return COMMAND_KEEP_CLIENT;
    }

    if (channel_mode_has(channel->modes, CHANNEL_MODE_NO_KNOCK)) {
        client_sendf(client, ERR_CANNOTKNOCK,
                     server->config.server_name, client->nick,
                     channel->name, "channel mode +K");
        return COMMAND_KEEP_CLIENT;
    }

    if (channel_client_is_banned(channel, client)) {
        client_sendf(client, ERR_CANNOTKNOCK,
                     server->config.server_name, client->nick,
                     channel->name, "you are banned");
        return COMMAND_KEEP_CLIENT;
    }

    is_full = channel->user_limit != 0U &&
              channel->member_count >= channel->user_limit;

    /* If no obvious access restriction is active, the requester can JOIN. */
    if (!channel_mode_has(channel->modes, CHANNEL_MODE_INVITE_ONLY) &&
        channel->key[0] == '\0' && !is_full) {
        client_sendf(client, ERR_CANNOTKNOCK,
                     server->config.server_name, client->nick,
                     channel->name, "channel is open");
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
        client_sendf(member->client, ":%s!%s@%s KNOCK %s :%s",
                     client->nick, client->user, client->display_host,
                     channel->name, reason);
        delivered = 1;
    }

    if (delivered) {
        client_sendf(client, ":%s NOTICE %s :KNOCK delivered to %s channel staff",
                     server->config.server_name, client->nick, channel->name);
    } else {
        client_sendf(client, ERR_CANNOTKNOCK,
                     server->config.server_name, client->nick,
                     channel->name, "no channel staff are available");
    }
    return COMMAND_KEEP_CLIENT;
}
