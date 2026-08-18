/**
 * @file kill.c
 * @brief IRC KILL command for authorized operators.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

CommandResult command_kill(Server *server, Client *client, char *params) {
    char *nick;
    char *reason;
    Client *target;
    char quit_reason[IRC_QUIT_REASON_MAX + 1U];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_KILL)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name, client->nick, "KILL");
        return COMMAND_KEEP_CLIENT;
    }

    nick = strtok(params, " ");
    reason = strtok(NULL, "");
    if (reason != NULL && *reason == ':') ++reason;
    if (nick == NULL || reason == NULL || *reason == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name, client->nick, "KILL");
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK, server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (client_mode_has(target->modes, CLIENT_MODE_SERVICE) ||
        (client_mode_has(target->modes, CLIENT_MODE_NETADMIN) &&
         !client_mode_has(client->modes, CLIENT_MODE_NETADMIN))) {
        client_sendf(client, ERR_KILLDENY, server->config.server_name,
                     client->nick, target->nick);
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(quit_reason, sizeof(quit_reason), "Killed (%s (%s))",
                   client->nick, reason);
    client_sendf(target, ":%s KILL %s :%s",
                 server->config.server_name, target->nick, reason);

    if (target == client) {
        (void)snprintf(client->quit_reason, sizeof(client->quit_reason), "%s", quit_reason);
        return COMMAND_DISCONNECT_CLIENT;
    }

    server_disconnect(server, target, quit_reason);
    return COMMAND_KEEP_CLIENT;
}
