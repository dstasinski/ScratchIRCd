/**
 * @file userhost.c
 * @brief Implementation of IRC USERHOST.
 */

#include "commands.h"
#include "numerics.h"

#include <string.h>

CommandResult command_userhost(Server *server, Client *client, char *params) {
    char *nick;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "USERHOST");
        return COMMAND_KEEP_CLIENT;
    }

    for (nick = strtok(params, " "); nick != NULL; nick = strtok(NULL, " ")) {
        Client *target = hash_get(&server->clients_by_nick, nick);
        if (target != NULL && target->registered) {
            client_sendf(client, RPL_USERHOST,
                         server->config.server_name, client->nick,
                         target->nick, target->user, target->host);
        }
    }
    return COMMAND_KEEP_CLIENT;
}
