/**
 * @file away.c
 * @brief Implementation of the IRC AWAY command.
 *
 * A non-empty trailing parameter stores the client's away message and returns
 * RPL_NOWAWAY (306).  AWAY with no parameter, or an empty trailing parameter,
 * clears the state and returns RPL_UNAWAY (305).
 */

#include "commands.h"
#include "ircv3.h"
#include "numerics.h"

#include <stdio.h>

CommandResult command_away(Server *server, Client *client, char *params) {
    const char *message = params;

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (message != NULL && *message == ':') {
        ++message;
    }

    if (message == NULL || *message == '\0') {
        client->away[0] = '\0';
        client_sendf(client, RPL_UNAWAY,
                     server->config.server_name, client->nick);
        ircv3_away_notify(client);
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(client->away, sizeof(client->away), "%s", message);
    client_sendf(client, RPL_NOWAWAY,
                 server->config.server_name, client->nick);
    ircv3_away_notify(client);
    return COMMAND_KEEP_CLIENT;
}
