/**
 * @file channel_log_commands.c
 * @brief Logging wrappers around mature JOIN/PART/QUIT handlers.
 */

#include "commands.h"
#include "channel_log.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CommandResult command_join_core(Server *, Client *, char *);
CommandResult command_part_core(Server *, Client *, char *);
CommandResult command_quit_core(Server *, Client *, char *);

static int pointer_was_present(Channel *channel, Channel **before, size_t count) {
    size_t i;
    for (i = 0U; i < count; ++i)
        if (before[i] == channel) return 1;
    return 0;
}

CommandResult command_join(Server *server, Client *client, char *params) {
    Channel **before = NULL;
    size_t before_count = client != NULL ? client->channel_count : 0U;
    size_t used = 0U;
    ClientChannelLink *link;
    CommandResult result;

    if (client != NULL && before_count > 0U) {
        before = calloc(before_count, sizeof(*before));
        if (before != NULL) {
            for (link = client->channels; link != NULL && used < before_count;
                 link = link->next)
                before[used++] = link->channel;
        }
    }

    result = command_join_core(server, client, params);

    if (client != NULL) {
        for (link = client->channels; link != NULL; link = link->next) {
            if (before == NULL || !pointer_was_present(link->channel, before, used))
                channel_log_join(server, link->channel, client);
        }
    }
    free(before);
    return result;
}

CommandResult command_part(Server *server, Client *client, char *params) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *name = NULL;
    char *reason = NULL;
    Channel *channel = NULL;
    int should_log = 0;

    if (params != NULL) {
        (void)snprintf(copy, sizeof(copy), "%s", params);
        name = strtok(copy, " ");
        reason = strtok(NULL, "");
        if (reason != NULL && *reason == ':') ++reason;
        if (name != NULL) {
            channel = hash_get(&server->channels_by_name, name);
            should_log = channel != NULL && channel_has_client(channel, client);
        }
    }

    if (should_log)
        channel_log_part(server, channel, client,
                         reason != NULL ? reason : IRC_DEFAULT_PART_REASON);
    return command_part_core(server, client, params);
}

CommandResult command_quit(Server *server, Client *client, char *params) {
    const char *reason = params;
    ClientChannelLink *link;

    if (reason != NULL && *reason == ':') ++reason;
    if (reason == NULL || *reason == '\0') reason = IRC_DEFAULT_QUIT_REASON;

    if (client != NULL && client->registered) {
        for (link = client->channels; link != NULL; link = link->next)
            channel_log_quit(server, link->channel, client, reason);
    }
    return command_quit_core(server, client, params);
}
