/**
 * @file list.c
 * @brief Implementation of the IRC LIST command.
 *
 * Unqualified LIST omits '&' channels and hides +p/+s channels from clients
 * that are neither members nor IRC operators.  Topic text is included for
 * channels the requester is allowed to enumerate.
 */

#include "commands.h"
#include "numerics.h"
#include "visibility.h"

#include <stdio.h>
#include <string.h>

static void send_list_entry(Server *server, Client *client, Channel *channel) {
    char topic[IRC_CHANNEL_TOPIC_MAX + 1U];
    const char *source;
    size_t topic_length;
    size_t topic_limit;
    int overhead;

    if (server == NULL || client == NULL || channel == NULL) return;
    source = channel->topic[0] != '\0' ? channel->topic : "";
    overhead = snprintf(NULL, 0, RPL_LIST,
                        server->config.server_name, client->nick,
                        channel->name, (int)channel->member_count, "");
    if (overhead < 0 || (size_t)overhead > IRC_LINE_CONTENT_MAX) return;
    topic_limit = IRC_LINE_CONTENT_MAX - (size_t)overhead;
    if (topic_limit > IRC_CHANNEL_TOPIC_MAX) topic_limit = IRC_CHANNEL_TOPIC_MAX;
    topic_length = strlen(source);
    if (topic_length > topic_limit) topic_length = topic_limit;
    memcpy(topic, source, topic_length);
    topic[topic_length] = '\0';

    client_sendf(client, RPL_LIST,
                 server->config.server_name, client->nick,
                 channel->name, (int)channel->member_count, topic);
}

CommandResult command_list(Server *server, Client *client, char *params) {
    (void)params;

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, RPL_LISTSTART,
                 server->config.server_name, client->nick);

    for (size_t bucket = 0U;
         bucket < server->channels_by_name.bucket_count && !client->output_overflowed;
         ++bucket) {
        HashEntry *entry;
        for (entry = server->channels_by_name.buckets[bucket]; entry != NULL;
             entry = entry->next) {
            Channel *channel = entry->value;
            if (!visibility_list_channel(client, channel)) {
                continue;
            }
            send_list_entry(server, client, channel);
            if (client->output_overflowed) break;
        }
    }

    if (!client->output_overflowed)
        client_sendf(client, RPL_LISTEND,
                     server->config.server_name, client->nick);
    return COMMAND_KEEP_CLIENT;
}
