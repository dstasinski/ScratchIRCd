/**
 * @file invite.c
 * @brief Implementation of the IRC INVITE command.
 *
 * Explicit invitations are one-use records stored by stable Client.id.  This
 * allows a user to change nickname after being invited without losing access
 * or transferring the invitation to another connection that takes the old
 * nickname.  Channel +V forbids INVITE entirely.
 */

#include "commands.h"
#include "channel_policy.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_invite(Server *server, Client *client, char *params) {
    char *nick;
    char *channel_name;
    Client *target;
    Channel *channel;
    ChannelMember *membership;
    char message[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "INVITE");
        return COMMAND_KEEP_CLIENT;
    }

    nick = strtok(params, " ");
    channel_name = strtok(NULL, " ");
    if (nick == NULL || channel_name == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "INVITE");
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK,
                     server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }

    channel = hash_get(&server->channels_by_name, channel_name);
    if (channel == NULL) {
        client_sendf(client, ERR_NOSUCHCHANNEL,
                     server->config.server_name, client->nick, channel_name);
        return COMMAND_KEEP_CLIENT;
    }

    membership = channel_find_member(channel, client);
    if (membership == NULL) {
        client_sendf(client, ERR_NOTONCHANNEL,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    if (!channel_privilege_has(membership->privileges,
                               CHANNEL_PRIV_HALFOP |
                               CHANNEL_PRIV_OPERATOR |
                               CHANNEL_PRIV_OWNER)) {
        client_sendf(client, ERR_CHANOPRIVSNEEDED,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    if (channel_mode_has(channel->modes, CHANNEL_MODE_NO_INVITE)) {
        client_sendf(client, ERR_518,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    if (channel_has_client(channel, target)) {
        client_sendf(client, ERR_USERONCHANNEL,
                     server->config.server_name, client->nick,
                     target->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    if (channel_invite_add(channel, target->id) != 0) {
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, RPL_INVITING,
                 server->config.server_name, client->nick,
                 target->nick, channel->name);

    (void)snprintf(message, sizeof(message),
                   ":%s!%s@%s INVITE %s :%s",
                   client->nick, client->user, client->host,
                   target->nick, channel->name);
    (void)client_send_line(target, message);

    return COMMAND_KEEP_CLIENT;
}
