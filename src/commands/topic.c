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
#include "irc.h"
#include "ircv3.h"
#include "modes.h"
#include "numerics.h"
#include "visibility.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static void send_no_such_channel_query(Server *server, Client *client,
                                       const char *query) {
    int base_length;
    size_t length;
    if (server == NULL || client == NULL || query == NULL) return;
    base_length = snprintf(NULL, 0, ":%s 403 %s  :No such channel",
                           server->config.server_name, client->nick);
    if (base_length < 0 || (size_t)base_length > IRC_LINE_CONTENT_MAX) return;
    length = strlen(query);
    if (length > IRC_LINE_CONTENT_MAX - (size_t)base_length)
        length = IRC_LINE_CONTENT_MAX - (size_t)base_length;
    client_sendf(client, ":%s 403 %s %.*s :No such channel",
                 server->config.server_name, client->nick,
                 (int)length, query);
}

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
        send_no_such_channel_query(server, client, channel_name);
        return COMMAND_KEEP_CLIENT;
    }

    if (topic == NULL) {
        /* Do not let a direct TOPIC query bypass private/secret/local-channel
         * visibility.  Members and IRC operators may still inspect it. */
        if (!visibility_names_channel(client, channel)) {
            client_sendf(client, ERR_NOSUCHCHANNEL,
                         server->config.server_name, client->nick,
                         channel->name);
            return COMMAND_KEEP_CLIENT;
        }
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

    /* TOPICLEN is derived from the configured server-name envelope so every
     * accepted topic is guaranteed to fit both a later 332 reply and every
     * legal source-prefixed live TOPIC relay. */
    if (strlen(topic) > irc_topic_limit(server) ||
        !ircv3_message_wire_fits(client, "TOPIC", channel->name, topic)) {
        client_sendf(client,
                     ":%s 417 %s TOPIC :Topic would exceed the IRC line limit",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

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
