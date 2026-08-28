/**
 * @file die.c
 * @brief Gracefully shut down ScratchIRCd.
 *
 * DIE never exits the process directly from command dispatch. It marks the
 * Server for shutdown and also triggers the existing event-loop exit path.
 * main() distinguishes shutdown from restart and performs normal teardown.
 */

#include "commands.h"
#include "message_policy.h"
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

    snotice_broadcast(server, SNOTICE_ADMIN,
                      "DIE requested by %s from %s",
                      client->nick, client->real_ip);
    client_sendf(client, ":%s NOTICE %s :Shutting down ScratchIRCd",
                 server->config.server_name, client->nick);
    /* Channel-log rows are durable on enqueue. main() performs one bounded
     * post-disconnect best-effort flush; draining the complete durable backlog
     * here would block the single event loop for an unbounded amount of time. */
    server->shutdown_requested = 1;
    server->restart_requested = 1;
    return COMMAND_KEEP_CLIENT;
}
