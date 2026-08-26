/**
 * @file setident.c
 * @brief Change a connected client's displayed IRC ident/user field.
 */

#include "commands.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int valid_ident(const char *ident) {
    const unsigned char *p = (const unsigned char *)ident;

    if (ident == NULL || *ident == '\0' || strlen(ident) > IRC_USER_MAX) return 0;
    for (; *p != '\0'; ++p) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.') continue;
        return 0;
    }
    return 1;
}

CommandResult command_setident(Server *server, Client *client, char *params) {
    char *nick;
    char *ident;
    Client *target;
    char old_ident[IRC_USER_MAX + 1U];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_OVERRIDE)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (nick = strtok(params, " ")) == NULL ||
        (ident = strtok(NULL, " ")) == NULL || !valid_ident(ident)) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "SETIDENT");
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK, server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(old_ident, sizeof(old_ident), "%s", target->user);
    (void)snprintf(target->user, sizeof(target->user), "%s", ident);
    client_sendf(client, ":%s NOTICE %s :SETIDENT %s -> %s",
                 server->config.server_name, client->nick, target->nick, target->user);
    client_sendf(target, ":%s NOTICE %s :Your ident is now %s",
                 server->config.server_name, target->nick, target->user);
    snotice_broadcast(server, SNOTICE_IDENTITY,
                      "SETIDENT by %s: %s %s -> %s [real_ip=%s]",
                      client->nick, target->nick, old_ident,
                      target->user, target->real_ip);
    return COMMAND_KEEP_CLIENT;
}
