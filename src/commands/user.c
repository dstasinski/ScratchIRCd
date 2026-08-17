/**
 * @file user.c
 * @brief Implementation of the IRC USER command.
 *
 * USER supplies the username/ident and real-name fields used to finish client
 * registration.  This early server stores the traditional middle USER fields
 * but does not assign semantic meaning to them yet.  A client may not issue
 * USER again after registration has completed.
 */

#include "commands.h"
#include "config.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_user(Server *server, Client *client, char *params) {
    char *user;
    char *mode;
    char *unused;
    char *realname;

    (void)server;

    if (client->registered) {
        client_sendf(client, ERR_ALREADYREGISTRED,
                     IRCD_SERVER_NAME, command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     IRCD_SERVER_NAME, command_reply_nick(client), "USER");
        return COMMAND_KEEP_CLIENT;
    }

    user = strtok(params, " ");
    mode = strtok(NULL, " ");
    unused = strtok(NULL, " ");
    realname = strtok(NULL, "");

    if (user == NULL || mode == NULL || unused == NULL || realname == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     IRCD_SERVER_NAME, command_reply_nick(client), "USER");
        return COMMAND_KEEP_CLIENT;
    }

    if (*realname == ':') {
        ++realname;
    }

    snprintf(client->user, sizeof(client->user), "%s", user);
    snprintf(client->realname, sizeof(client->realname), "%s", realname);

    command_maybe_register(client);
    return COMMAND_KEEP_CLIENT;
}
