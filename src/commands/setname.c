/**
 * @file setname.c
 * @brief Change a connected client's real-name/gecos field.
 */

#include "commands.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

CommandResult command_setname(Server *server, Client *client, char *params) {
    char *nick;
    char *realname;
    Client *target;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_OVERRIDE)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (nick = strtok(params, " ")) == NULL ||
        (realname = strtok(NULL, "")) == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "SETNAME");
        return COMMAND_KEEP_CLIENT;
    }
    if (*realname == ':') ++realname;
    if (*realname == '\0' || strlen(realname) > IRC_REALNAME_MAX) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "SETNAME");
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK, server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(target->realname, sizeof(target->realname), "%s", realname);
    client_sendf(client, ":%s NOTICE %s :SETNAME completed for %s",
                 server->config.server_name, client->nick, target->nick);
    client_sendf(target, ":%s NOTICE %s :Your real name is now %s",
                 server->config.server_name, target->nick, target->realname);
    return COMMAND_KEEP_CLIENT;
}
