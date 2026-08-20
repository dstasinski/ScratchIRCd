/**
 * @file topic.c
 * @brief Implementation of the IRC TOPIC command.
 *
 * TOPIC with no trailing topic queries current state using numerics.h. Setting
 * a topic requires channel membership. When channel mode +t is set, halfop or
 * greater privilege is required. The stored setter and broadcast prefix use
 * display_host because topic metadata is client-visible IRC state.
 */

#include "commands.h"
#include "config.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

CommandResult command_topic(Server *server, Client *client, char *params) {
    char *channel_name;
    char *topic;
    Channel *channel;
    ChannelMember *member;
    char message[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "TOPIC");
        return COMMAND_KEEP_CLIENT;
    }

    channel_name = strtok(params, " ");
    topic = strtok(NULL, "");
    if (channel_name == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "TOPIC");
        return COMMAND_KEEP_CLIENT;
    }

    channel = hash_get(&server->channels_by_name, channel_name);
    if (channel == NULL) {
        client_sendf(client, ERR_NOSUCHCHANNEL,
                     server->config.server_name, client->nick, channel_name);
        return COMMAND_KEEP_CLIENT;
    }

    if (topic == NULL) {
        if (channel->topic[0] == '\0') {
            client_sendf(client, RPL_NOTOPIC,
                         server->config.server_name, client->nick,
                         channel->name);
        } else {
            client_sendf(client, RPL_TOPIC,
                         server->config.server_name, client->nick,
                         channel->name, channel->topic);
            client_sendf(client, RPL_TOPICWHOTIME,
                         server->config.server_name, client->nick,
                         channel->name, channel->topic_setter,
                         (unsigned long)channel->topic_time);
        }
        return COMMAND_KEEP_CLIENT;
    }

    member = channel_find_member(channel, client);
    if (member == NULL) {
        client_sendf(client, ERR_NOTONCHANNEL,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    if (channel_mode_has(channel->modes, CHANNEL_MODE_TOPIC_LOCK) &&
        channel_privilege_rank(member->privileges) < 2U) {
        client_sendf(client, ERR_CHANOPRIVSNEEDED,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    if (*topic == ':') ++topic;

    (void)snprintf(channel->topic, sizeof(channel->topic), "%s", topic);
    (void)snprintf(channel->topic_setter, sizeof(channel->topic_setter),
                   "%s!%s@%s", client->nick, client->user, client->display_host);
    channel->topic_time = time(NULL);

    (void)snprintf(message, sizeof(message),
                   ":%s!%s@%s TOPIC %s :%s\r\n",
                   client->nick, client->user, client->display_host,
                   channel->name, channel->topic);
    channel_broadcast(channel, NULL, message);
    return COMMAND_KEEP_CLIENT;
}
