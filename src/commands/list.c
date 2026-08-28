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
            client_sendf(client, RPL_LIST,
                         server->config.server_name, client->nick,
                         channel->name, (int)channel->member_count,
                         channel->topic[0] != '\0' ? channel->topic : "");
            if (client->output_overflowed) break;
        }
    }

    if (!client->output_overflowed)
        client_sendf(client, RPL_LISTEND,
                     server->config.server_name, client->nick);
    return COMMAND_KEEP_CLIENT;
}
