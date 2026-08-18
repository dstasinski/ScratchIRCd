/**
 * @file names.c
 * @brief Implementation of the IRC NAMES command.
 *
 * NAMES uses the shared visibility policy for explicit queries and preserves
 * ScratchIRCd's membership prefixes: ~ owner, @ operator, % halfop, + voice.
 */

#include "commands.h"
#include "config.h"
#include "numerics.h"
#include "visibility.h"

#include <stdio.h>
#include <string.h>

static void send_names_for_channel(Server *server, Client *client,
                                   Channel *channel) {
    char names[IRC_NAMES_BUFFER_SIZE];
    size_t used = 0U;
    ChannelMember *member;
    char marker;

    if (!visibility_names_channel(client, channel)) {
        client_sendf(client, RPL_ENDOFNAMES, server->config.server_name,
                     client->nick, channel->name);
        return;
    }

    names[0] = '\0';
    for (member = channel->members; member != NULL; member = member->next) {
        char prefix = channel_privilege_prefix(member->privileges);
        int written = snprintf(names + used, sizeof(names) - used,
                               "%s%s%s", used != 0U ? " " : "",
                               prefix != '\0' ? (char[2]){prefix, '\0'} : "",
                               member->client->nick);
        if (written < 0 || (size_t)written >= sizeof(names) - used) {
            break;
        }
        used += (size_t)written;
    }

    marker = channel_mode_has(channel->modes, CHANNEL_MODE_SECRET) ? '@' :
             channel_mode_has(channel->modes, CHANNEL_MODE_PRIVATE) ||
             channel->name[0] == '&' ? IRC_NAMES_PRIVATE_MARKER :
             IRC_NAMES_PUBLIC_MARKER;

    client_sendf(client, RPL_NAMREPLY, server->config.server_name,
                 client->nick, marker, channel->name, names);
    client_sendf(client, RPL_ENDOFNAMES, server->config.server_name,
                 client->nick, channel->name);
}

CommandResult command_names(Server *server, Client *client, char *params) {
    char *name;

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (params != NULL && (name = strtok(params, " ,")) != NULL) {
        Channel *channel = hash_get(&server->channels_by_name, name);
        if (channel != NULL) {
            send_names_for_channel(server, client, channel);
        } else {
            client_sendf(client, RPL_ENDOFNAMES, server->config.server_name,
                         client->nick, name);
        }
        return COMMAND_KEEP_CLIENT;
    }

    for (size_t bucket = 0U; bucket < server->channels_by_name.bucket_count;
         ++bucket) {
        HashEntry *entry;
        for (entry = server->channels_by_name.buckets[bucket]; entry != NULL;
             entry = entry->next) {
            Channel *channel = entry->value;
            if (visibility_names_channel(client, channel)) {
                send_names_for_channel(server, client, channel);
            }
        }
    }
    return COMMAND_KEEP_CLIENT;
}
