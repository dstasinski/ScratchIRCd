/**
 * @file lusers.c
 * @brief Implementation of IRC LUSERS for this single-server daemon.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"

#include <stddef.h>
#include <time.h>

CommandResult command_lusers(Server *server, Client *client, char *params) {
    static const Server *highwater_server = NULL;
    static time_t highwater_started_at = 0;
    static size_t highwater_users = 0U;
    size_t users = 0U;
    size_t invisible = 0U;
    size_t opers = 0U;
    size_t unknown = 0U;
    size_t channels = 0U;
    size_t i;

    (void)params;
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    for (i = 0U; i < server->client_count; ++i) {
        Client *target = server->clients[i];
        if (target == NULL) continue;
        if (!target->registered) {
            ++unknown;
            continue;
        }
        ++users;
        if (client_mode_has(target->modes, CLIENT_MODE_INVISIBLE)) ++invisible;
        if (client_mode_has(target->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) ++opers;
    }

    if (highwater_server != server || highwater_started_at != server->started_at) {
        highwater_server = server;
        highwater_started_at = server->started_at;
        highwater_users = 0U;
    }
    if (users > highwater_users) highwater_users = users;

    for (i = 0U; i < server->channels_by_name.bucket_count; ++i) {
        HashEntry *entry;
        for (entry = server->channels_by_name.buckets[i]; entry != NULL; entry = entry->next) {
            ++channels;
        }
    }

    client_sendf(client, RPL_LUSERCLIENT, server->config.server_name, client->nick,
                 (int)(users - invisible), (int)invisible, 1);
    if (opers != 0U)
        client_sendf(client, RPL_LUSEROP, server->config.server_name, client->nick, (int)opers);
    if (unknown != 0U)
        client_sendf(client, RPL_LUSERUNKNOWN, server->config.server_name, client->nick, (int)unknown);
    client_sendf(client, RPL_LUSERCHANNELS, server->config.server_name, client->nick, (int)channels);
    client_sendf(client, RPL_LUSERME, server->config.server_name, client->nick,
                 (int)users, 0);
    client_sendf(client, RPL_LOCALUSERS, server->config.server_name, client->nick,
                 (int)users, (int)highwater_users);
    client_sendf(client, RPL_GLOBALUSERS, server->config.server_name, client->nick,
                 (int)users, (int)highwater_users);
    return COMMAND_KEEP_CLIENT;
}
