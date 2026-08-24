/**
 * @file sethost.c
 * @brief Change only the public/displayed hostname of a client.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"
#include "oper.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int valid_display_host(const char *host) {
    const unsigned char *p = (const unsigned char *)host;

    if (host == NULL || *host == '\0' || strlen(host) > IRC_HOST_MAX) return 0;
    for (; *p != '\0'; ++p) {
        if (isalnum(*p) || *p == '.' || *p == '-' || *p == '_' || *p == ':') continue;
        return 0;
    }
    return 1;
}

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
        (host = strtok(NULL, " ")) == NULL || !valid_display_host(host)) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "SETHOST");
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, nick);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK, server->config.server_name, client->nick, nick);
        return COMMAND_KEEP_CLIENT;
    }

    /* SETHOST changes only the public identity. real_ip/real_host are immutable here. */
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
