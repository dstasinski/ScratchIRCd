/**
 * @file joinpart.c
 * @brief Implementations of the IRC JOIN and PART channel-membership commands.
 *
 * JOIN and PART are kept together because they are inverse operations over the
 * same bidirectional Client/Channel membership structures.  JOIN creates a
 * channel on demand and sends the appropriate topic/NAMES numerics.  PART
 * removes membership and asks the server to destroy an empty channel.
 *
 * This iteration supports one #channel target per JOIN or PART command.
 */

#include "commands.h"
#include "config.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_join(Server *server, Client *client, char *params) {
    Channel *channel;
    char message[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL || params[0] != IRC_CHANNEL_PREFIX ||
        strlen(params) > IRC_CHANNEL_NAME_MAX || strchr(params, ' ') != NULL) {
        client_sendf(client, ERR_NOSUCHCHANNEL,
                     IRCD_SERVER_NAME, client->nick,
                     params != NULL ? params : "*");
        return COMMAND_KEEP_CLIENT;
    }

    if (client->channel_count >= IRC_MAX_CHANNELS_PER_CLIENT) {
        client_sendf(client, ERR_TOOMANYCHANNELS,
                     IRCD_SERVER_NAME, client->nick, params);
        return COMMAND_KEEP_CLIENT;
    }

    channel = server_get_or_create_channel(server, params);
    if (channel == NULL || channel_add_client(channel, client) < 0) {
        return COMMAND_KEEP_CLIENT;
    }

    snprintf(message, sizeof(message), ":%s!%s@%s JOIN %s\r\n",
             client->nick, client->user, client->host, channel->name);
    channel_broadcast(channel, NULL, message);

    client_sendf(client, RPL_NOTOPIC,
                 IRCD_SERVER_NAME, client->nick, channel->name);
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
                     IRCD_SERVER_NAME, client->nick, "PART");
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
                     IRCD_SERVER_NAME, client->nick, name);
        return COMMAND_KEEP_CLIENT;
    }

    if (!channel_has_client(channel, client)) {
        client_sendf(client, ERR_NOTONCHANNEL,
                     IRCD_SERVER_NAME, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    snprintf(message, sizeof(message), ":%s!%s@%s PART %s :%s\r\n",
             client->nick, client->user, client->host, channel->name,
             reason != NULL ? reason : IRC_DEFAULT_PART_REASON);
    channel_broadcast(channel, NULL, message);

    channel_remove_client(channel, client);
    server_remove_channel_if_empty(server, channel);
    return COMMAND_KEEP_CLIENT;
}
