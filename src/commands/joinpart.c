/**
 * @file joinpart.c
 * @brief Implementations of IRC JOIN and PART.
 *
 * Both standard '#' channels and private/unlisted '&' channels are valid.
 * JOIN performs the mode checks whose required state already exists: channel
 * key (+k), user limit (+l), registered-only (+R), oper-only (+O), admin-only
 * (+A), and secure-only (+z). Invite state, join-throttle history, and ban
 * matching are intentionally deferred to their dedicated subsystems.
 */

#include "commands.h"
#include "config.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

static int valid_channel_name(const char *name) {
    return name != NULL && strchr(IRC_CHANNEL_PREFIXES, name[0]) != NULL &&
           strlen(name) <= IRC_CHANNEL_NAME_MAX && strchr(name, ' ') == NULL;
}

CommandResult command_join(Server *server, Client *client, char *params) {
    Channel *channel;
    char *name;
    char *key;
    char message[IRCD_MESSAGE_BUFFER_SIZE];
    int first_member;

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "JOIN");
        return COMMAND_KEEP_CLIENT;
    }

    name = strtok(params, " ");
    key = strtok(NULL, " ");

    if (!valid_channel_name(name)) {
        client_sendf(client, ERR_NOSUCHCHANNEL,
                     server->config.server_name, client->nick,
                     name != NULL ? name : "*");
        return COMMAND_KEEP_CLIENT;
    }

    if (client->channel_count >= IRC_MAX_CHANNELS_PER_CLIENT) {
        client_sendf(client, ERR_TOOMANYCHANNELS,
                     server->config.server_name, client->nick, name);
        return COMMAND_KEEP_CLIENT;
    }

    channel = hash_get(&server->channels_by_name, name);
    if (channel != NULL) {
        if (channel->key[0] != '\0' &&
            (key == NULL || strcmp(channel->key, key) != 0)) {
            client_sendf(client, ERR_BADCHANNELKEY,
                         server->config.server_name, client->nick, channel->name);
            return COMMAND_KEEP_CLIENT;
        }
        if (channel->user_limit != 0U &&
            channel->member_count >= channel->user_limit) {
            client_sendf(client, ERR_CHANNELISFULL,
                         server->config.server_name, client->nick, channel->name);
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_REGONLY_JOIN) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED)) {
            client_sendf(client, ERR_NEEDREGGEDNICK,
                         server->config.server_name, client->nick, channel->name);
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_OPER_ONLY) &&
            !client_mode_has(client->modes,
                             CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
            client_sendf(client, ERR_520,
                         server->config.server_name, client->nick, channel->name);
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_ADMIN_ONLY) &&
            !client_mode_has(client->modes, CLIENT_MODE_NETADMIN)) {
            client_sendf(client, ERR_519,
                         server->config.server_name, client->nick, channel->name);
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_SECURE_ONLY) &&
            !client_mode_has(client->modes, CLIENT_MODE_SECURE)) {
            client_sendf(client, ERR_SECUREONLYCHAN,
                         server->config.server_name, client->nick, channel->name);
            return COMMAND_KEEP_CLIENT;
        }
    } else {
        channel = server_get_or_create_channel(server, name);
    }

    if (channel == NULL) {
        return COMMAND_KEEP_CLIENT;
    }

    first_member = channel->member_count == 0U;
    if (channel_add_client(channel, client) < 0) {
        return COMMAND_KEEP_CLIENT;
    }

    if (first_member && !channel_mode_has(channel->modes, CHANNEL_MODE_REGISTERED)) {
        (void)channel_add_privileges(channel, client,
                                     CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR);
    }

    (void)snprintf(message, sizeof(message), ":%s!%s@%s JOIN %s\r\n",
                   client->nick, client->user, client->host, channel->name);
    channel_broadcast(channel, NULL, message);

    client_sendf(client, RPL_NOTOPIC,
                 server->config.server_name, client->nick, channel->name);
    command_send_names(channel, client);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_part(Server *server, Client *client, char *params) {
    char *name;
    char *reason;
    Channel *channel;
    char message[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "PART");
        return COMMAND_KEEP_CLIENT;
    }

    name = strtok(params, " ");
    reason = strtok(NULL, "");
    if (reason != NULL && *reason == ':') {
        ++reason;
    }

    channel = hash_get(&server->channels_by_name, name);
    if (channel == NULL) {
        client_sendf(client, ERR_NOSUCHCHANNEL,
                     server->config.server_name, client->nick, name);
        return COMMAND_KEEP_CLIENT;
    }

    if (!channel_has_client(channel, client)) {
        client_sendf(client, ERR_NOTONCHANNEL,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(message, sizeof(message), ":%s!%s@%s PART %s :%s\r\n",
                   client->nick, client->user, client->host, channel->name,
                   reason != NULL ? reason : IRC_DEFAULT_PART_REASON);
    channel_broadcast(channel, NULL, message);

    channel_remove_client(channel, client);
    server_remove_channel_if_empty(server, channel);
    return COMMAND_KEEP_CLIENT;
}
