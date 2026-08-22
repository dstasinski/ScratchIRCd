/**
 * @file silence.c
 * @brief Client-local SILENCE mask management.
 *
 * Syntax:
 *   SILENCE                 - list masks
 *   SILENCE +<mask> [...]   - add masks
 *   SILENCE -<mask> [...]   - remove masks
 */

#include "commands.h"
#include "numerics.h"
#include "presence.h"

#include <string.h>

static void list_masks(Server *server, Client *client) {
    ClientSilenceEntry *entry;
    for (entry = client->silence_list; entry != NULL; entry = entry->next)
        client_sendf(client, RPL_SILELIST, server->config.server_name,
                     client->nick, client->nick, entry->mask);
    client_sendf(client, RPL_ENDOFSILELIST, server->config.server_name,
                 client->nick);
}

CommandResult command_silence(Server *server, Client *client, char *params) {
    char *token;
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        list_masks(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    for (token = strtok(params, " "); token != NULL; token = strtok(NULL, " ")) {
        const char *mask;
        int rc;
        if ((token[0] != '+' && token[0] != '-') || token[1] == '\0') continue;
        mask = token + 1;
        if (strlen(mask) > IRC_CHANNEL_MASK_MAX) continue;
        if (token[0] == '+') {
            rc = presence_silence_add(client, mask);
            if (rc < 0) {
                client_sendf(client, ERR_SILELISTFULL, server->config.server_name,
                             client->nick, mask);
                break;
            }
        } else {
            (void)presence_silence_remove(client, mask);
        }
    }
    return COMMAND_KEEP_CLIENT;
}
