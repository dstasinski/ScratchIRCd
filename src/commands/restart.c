/**
 * @file restart.c
 * @brief Request an in-process ScratchIRCd restart.
 *
 * The command does not reinitialize the daemon from inside command dispatch.
 * It marks Server.restart_requested; the event loop returns to main(), which
 * destroys the old server state, reloads ircd.conf, recreates databases/listeners,
 * and starts a fresh Server instance in the same process.
 */

#include "commands.h"
#include "channel_log.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"

CommandResult command_restart(Server *server, Client *client, char *params) {
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_RESTART)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    snotice_broadcast(server, SNOTICE_ADMIN,
                      "RESTART requested by %s from %s",
                      client->nick, client->real_ip);
    client_sendf(client, ":%s NOTICE %s :Restarting ScratchIRCd",
                 server->config.server_name, client->nick);
    channel_log_flush_all(server);
    server->restart_requested = 1;
    return COMMAND_KEEP_CLIENT;
}
