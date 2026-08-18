/**
 * @file pass.c
 * @brief Implementation of the IRC PASS command.
 *
 * PASS is accepted only before registration. When server_password is empty in
 * runtime configuration, PASS is optional and any supplied value is accepted.
 * When configured, a matching password unlocks registration and an incorrect
 * password produces ERR_PASSWDMISMATCH (464).
 */

#include "commands.h"
#include "numerics.h"

#include <string.h>

CommandResult command_pass(Server *server, Client *client, char *params) {
    char *password;

    if (client->registered) {
        client_sendf(client, ERR_ALREADYREGISTRED,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (password = strtok(params, " ")) == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, command_reply_nick(client), "PASS");
        return COMMAND_KEEP_CLIENT;
    }

    if (server->config.server_password[0] == '\0' ||
        strcmp(password, server->config.server_password) == 0) {
        client->pass_accepted = 1;
        command_maybe_register(server, client);
    } else {
        client->pass_accepted = 0;
        client_sendf(client, ERR_PASSWDMISMATCH,
                     server->config.server_name, command_reply_nick(client));
    }
    return COMMAND_KEEP_CLIENT;
}
