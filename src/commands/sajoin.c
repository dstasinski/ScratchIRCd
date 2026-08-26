/**
 * @file sajoin.c
 * @brief Server-authority channel join for operators with can_override.
 */

#include "commands.h"
#include "channel_log.h"
#include "chanserv.h"
#include "config.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

CommandResult command_sajoin(Server *server, Client *client, char *params) {
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
                     client->nick, "SAJOIN");
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK, server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }

    for (name = strtok(channels, ", "); name != NULL; name = strtok(NULL, ", ")) {
        Channel *channel;
        char message[IRCD_MESSAGE_BUFFER_SIZE];
        int first;

        if (!channel_name_valid(name) || target->channel_count >= IRC_MAX_CHANNELS_PER_CLIENT) continue;
        channel = server_get_or_create_channel(server, name);
        if (channel == NULL || channel_has_client(channel, target)) continue;
        first = channel->member_count == 0U;
        if (channel_add_client(channel, target) != 0) continue;
        if (channel_mode_has(channel->modes, CHANNEL_MODE_REGISTERED)) {
            ChannelPrivilegeSet privileges =
                chanserv_client_privileges(server, target, channel->name);
            if (privileges != 0U) {
                (void)channel_add_privileges(channel, target, privileges);
            }
        } else if (first) {
            (void)channel_add_privileges(channel, target,
                                         CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR);
        }
        (void)snprintf(message, sizeof(message), ":%s!%s@%s JOIN %s\r\n",
                       target->nick, target->user, target->display_host, channel->name);
        channel_broadcast(channel, NULL, message);
        channel_log_join(server, channel, target);
        snotice_broadcast(server, SNOTICE_MODERATION,
                          "SAJOIN by %s: %s -> %s [real_ip=%s]",
                          client->nick, target->nick, channel->name, target->real_ip);
        if (channel->topic[0] != '\0') {
            client_sendf(target, RPL_TOPIC, server->config.server_name,
                         target->nick, channel->name, channel->topic);
        }
        command_send_names(server, channel, target);
    }

    client_sendf(client, ":%s NOTICE %s :SAJOIN completed for %s",
                 server->config.server_name, client->nick, target->nick);
    return COMMAND_KEEP_CLIENT;
}
