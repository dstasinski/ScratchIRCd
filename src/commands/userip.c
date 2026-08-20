/**
 * @file userip.c
 * @brief Operator-only implementation of IRC USERIP.
 *
 * real_ip is privileged identity data. Ordinary clients are permitted to see
 * only display_host, so USERIP requires IRC operator status. For WebIRC users
 * this will report the authenticated end-user IP, never the gateway address.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"

#include <string.h>

CommandResult command_userip(Server *server, Client *client, char *params) {
    char *nick;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "USERIP");
        return COMMAND_KEEP_CLIENT;
    }

    for (nick = strtok(params, " "); nick != NULL; nick = strtok(NULL, " ")) {
        Client *target = hash_get(&server->clients_by_nick, nick);
        if (target != NULL && target->registered) {
            client_sendf(client, RPL_USERIP,
                         server->config.server_name, client->nick,
                         target->nick, target->user, target->real_ip);
        }
    }
    return COMMAND_KEEP_CLIENT;
}
