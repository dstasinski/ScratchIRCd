/**
 * @file die.c
 * @brief Gracefully shut down ScratchIRCd.
 *
 * DIE never exits the process directly from command dispatch. It marks the
 * Server for shutdown; the event loop returns to main(), which performs the
 * normal server_destroy() cleanup path before the process exits.
 */

#include "commands.h"
#include "numerics.h"
#include "oper.h"

CommandResult command_die(Server *server, Client *client, char *params) {
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_DIE)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, ":%s NOTICE %s :Shutting down ScratchIRCd",
                 server->config.server_name, client->nick);
    server->shutdown_requested = 1;
    return COMMAND_KEEP_CLIENT;
}
