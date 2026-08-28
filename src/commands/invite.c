/**
 * @file invite.c
 * @brief Implementation of the IRC INVITE command.
 *
 * Explicit invitations are one-use records stored by stable Client.id.  This
 * allows a user to change nickname after being invited without losing access
 * or transferring the invitation to another connection that takes the old
 * nickname. Channel +V forbids INVITE entirely.
 */

#include "commands.h"
#include "channel_policy.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

static void send_invite_query_error(Server *server, Client *client,
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

CommandResult command_invite(Server *server, Client *client, char *params) {
    char *nick;
    char *channel_name;
    Client *target;
    Channel *channel;
    ChannelMember *membership;
    char message[IRCD_MESSAGE_BUFFER_SIZE];
    int invite_rc;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

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
    if (!channel_name_valid(channel_name)) {
        send_invite_query_error(server, client, 403, channel_name);
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL || !target->registered) {
        send_invite_query_error(server, client, 401, nick);
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

    /* INVITE authority begins at halfop and includes every higher rank. */
    if (!channel_privilege_has(membership->privileges,
                               CHANNEL_PRIV_HALFOP |
                               CHANNEL_PRIV_OPERATOR |
                               CHANNEL_PRIV_PROTECTED |
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

    invite_rc = channel_invite_add(channel, target->id);
    if (invite_rc == -2) {
        client_sendf(client, ":%s NOTICE %s :Channel %s pending invite list is full (maximum %u)",
                     server->config.server_name, client->nick, channel->name,
                     (unsigned)IRC_CHANNEL_INVITE_MAX);
        return COMMAND_KEEP_CLIENT;
    }
    if (invite_rc != 0) return COMMAND_KEEP_CLIENT;

    client_sendf(client, RPL_INVITING,
                 server->config.server_name, client->nick,
                 target->nick, channel->name);

    (void)snprintf(message, sizeof(message),
                   ":%s!%s@%s INVITE %s :%s",
                   client->nick, client->user, client->display_host,
                   target->nick, channel->name);
    (void)client_send_line(target, message);

    return COMMAND_KEEP_CLIENT;
}
