/**
 * @file sethost.c
 * @brief Change only the public/displayed hostname of a client.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

CommandResult command_sethost(Server *server, Client *client, char *params) {
    char *nick;
    char *host;
    Client *target;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_OVERRIDE)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (nick = strtok(params, " ")) == NULL ||
        (host = strtok(NULL, " ")) == NULL || *host == '\0' ||
        strlen(host) > IRC_HOST_MAX) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "SETHOST");
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK, server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(target->display_host, sizeof(target->display_host), "%s", host);
    target->modes = client_mode_add(target->modes, CLIENT_MODE_VHOST);
    target->modes = client_mode_remove(target->modes, CLIENT_MODE_CLOAKED);

    client_sendf(client, ":%s NOTICE %s :SETHOST %s -> %s",
                 server->config.server_name, client->nick,
                 target->nick, target->display_host);
    client_sendf(target, ":%s NOTICE %s :Your displayed hostname is now %s",
                 server->config.server_name, target->nick, target->display_host);
    return COMMAND_KEEP_CLIENT;
}
