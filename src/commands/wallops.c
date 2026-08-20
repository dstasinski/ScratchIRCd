/**
 * @file wallops.c
 * @brief Send a WALLOPS message to clients with user mode +w.
 */

#include "commands.h"
#include "numerics.h"
#include "oper.h"

#include <string.h>

CommandResult command_wallops(Server *server, Client *client, char *params) {
    size_t i;
    char *text = params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_WALLOPS)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (text == NULL || *text == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "WALLOPS");
        return COMMAND_KEEP_CLIENT;
    }
    if (*text == ':') ++text;

    for (i = 0U; i < server->client_count; ++i) {
        Client *target = server->clients[i];
        if (target->registered && client_mode_has(target->modes, CLIENT_MODE_WALLOPS)) {
            client_sendf(target, ":%s!%s@%s WALLOPS :%s",
                         client->nick, client->user, client->display_host, text);
        }
    }
    return COMMAND_KEEP_CLIENT;
}
