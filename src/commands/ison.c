/**
 * @file ison.c
 * @brief Implementation of IRC ISON for case-insensitive online checks.
 */

#include "commands.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_ison(Server *server, Client *client, char *params) {
    char online[IRCD_OUTPUT_BUFFER_SIZE] = "";
    char *nick;

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL) {
        client_sendf(client, RPL_ISON,
                     server->config.server_name, client->nick, "");
        return COMMAND_KEEP_CLIENT;
    }

    for (nick = strtok(params, " "); nick != NULL; nick = strtok(NULL, " ")) {
        Client *found = hash_get(&server->clients_by_nick, nick);
        if (found != NULL && found->registered) {
            size_t used = strlen(online);
            (void)snprintf(online + used, sizeof(online) - used, "%s%s",
                           used != 0U ? " " : "", found->nick);
        }
    }

    client_sendf(client, RPL_ISON,
                 server->config.server_name, client->nick, online);
    return COMMAND_KEEP_CLIENT;
}
