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

static int names_payload_fits(Server *server, Client *client, char marker,
                              Channel *channel, const char *names) {
    int written;
    if (server == NULL || client == NULL || channel == NULL || names == NULL) return 0;
    written = snprintf(NULL, 0, RPL_NAMREPLY,
                       server->config.server_name, client->nick,
                       marker, channel->name, names);
    return written >= 0 && (size_t)written <= IRC_LINE_CONTENT_MAX;
}

static void send_names_chunk(Server *server, Client *client, char marker,
                             Channel *channel, const char *names) {
    if (server == NULL || client == NULL || channel == NULL || names == NULL) return;
    client_sendf(client, RPL_NAMREPLY,
                 server->config.server_name, client->nick,
                 marker, channel->name, names);
}

static void send_names_for_channel(Server *server, Client *client,
                                   Channel *channel) {
    char names[IRC_LINE_CONTENT_MAX + 1U];
    char candidate[IRC_LINE_CONTENT_MAX + 1U];
    size_t used = 0U;
    ChannelMember *member;
    char marker;

    if (!visibility_names_channel(client, channel)) {
        client_sendf(client, RPL_ENDOFNAMES, server->config.server_name,
                     client->nick, channel->name);
        return;
    }

    marker = channel_mode_has(channel->modes, CHANNEL_MODE_SECRET) ? '@' :
             channel_mode_has(channel->modes, CHANNEL_MODE_PRIVATE) ||
             channel->name[0] == '&' ? IRC_NAMES_PRIVATE_MARKER :
             IRC_NAMES_PUBLIC_MARKER;

    names[0] = '\0';
    for (member = channel->members; member != NULL; member = member->next) {
        char token[IRC_NICK_MAX + 2U];
        char prefix = channel_privilege_prefix(member->privileges);
        int token_written = prefix != '\0' ?
            snprintf(token, sizeof(token), "%c%s", prefix, member->client->nick) :
            snprintf(token, sizeof(token), "%s", member->client->nick);
        int candidate_written;

        if (token_written < 0 || (size_t)token_written >= sizeof(token)) continue;
        candidate_written = snprintf(candidate, sizeof(candidate), "%s%s%s",
                                     names, used != 0U ? " " : "", token);
        if (candidate_written >= 0 &&
            (size_t)candidate_written < sizeof(candidate) &&
            names_payload_fits(server, client, marker, channel, candidate)) {
            memcpy(names, candidate, (size_t)candidate_written + 1U);
            used = (size_t)candidate_written;
            continue;
        }

        if (used != 0U) {
            send_names_chunk(server, client, marker, channel, names);
            if (client->output_overflowed) return;
        }
        (void)snprintf(names, sizeof(names), "%s", token);
        used = strlen(names);
    }

    if (used != 0U || channel->member_count == 0U) {
        send_names_chunk(server, client, marker, channel, names);
        if (client->output_overflowed) return;
    }

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

    for (size_t bucket = 0U;
         bucket < server->channels_by_name.bucket_count && !client->output_overflowed;
         ++bucket) {
        HashEntry *entry;
        for (entry = server->channels_by_name.buckets[bucket]; entry != NULL;
             entry = entry->next) {
            Channel *channel = entry->value;
            if (visibility_names_channel(client, channel)) {
                send_names_for_channel(server, client, channel);
                if (client->output_overflowed) break;
            }
        }
    }
    return COMMAND_KEEP_CLIENT;
}
