/**
 * @file setident.c
 * @brief Change a connected client's displayed IRC ident/user field.
 */

#include "commands.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

CommandResult command_setident(Server *server, Client *client, char *params) {
    char *nick;
    char *ident;
    Client *target;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_OVERRIDE)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (nick = strtok(params, " ")) == NULL ||
        (ident = strtok(NULL, " ")) == NULL || *ident == '\0' ||
        strlen(ident) > IRC_USER_MAX) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "SETIDENT");
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK, server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(target->user, sizeof(target->user), "%s", ident);
    client_sendf(client, ":%s NOTICE %s :SETIDENT %s -> %s",
                 server->config.server_name, client->nick, target->nick, target->user);
    client_sendf(target, ":%s NOTICE %s :Your ident is now %s",
                 server->config.server_name, target->nick, target->user);
    return COMMAND_KEEP_CLIENT;
}
