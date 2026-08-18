/**
 * @file user.c
 * @brief Implementation of the IRC USER command.
 *
 * USER supplies the ident and real-name fields. Registration is attempted
 * after parsing, but common.c waits for NICK and asynchronous DNS completion.
 */

#include "commands.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_user(Server *server, Client *client, char *params) {
    char *user;
    char *mode;
    char *unused;
    char *realname;

    if (client->registered) {
        client_sendf(client, ERR_ALREADYREGISTRED,
                     server->config.server_name, command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, command_reply_nick(client), "USER");
        return COMMAND_KEEP_CLIENT;
    }

    user = strtok(params, " ");
    mode = strtok(NULL, " ");
    unused = strtok(NULL, " ");
    realname = strtok(NULL, "");

    if (user == NULL || mode == NULL || unused == NULL || realname == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, command_reply_nick(client), "USER");
        return COMMAND_KEEP_CLIENT;
    }

    if (*realname == ':') {
        ++realname;
    }

    (void)snprintf(client->user, sizeof(client->user), "%s", user);
    (void)snprintf(client->realname, sizeof(client->realname), "%s", realname);

    command_maybe_register(server, client);
    return COMMAND_KEEP_CLIENT;
}
