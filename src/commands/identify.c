/**
 * @file identify.c
 * @brief Direct IDENTIFY alias for NickServ account authentication.
 *
 * Syntax:
 *   IDENTIFY <password>
 *   IDENTIFY <nick> <password>
 */

#include "commands.h"
#include "chanserv.h"
#include "ircv3.h"
#include "memoserv.h"
#include "nickserv.h"
#include "numerics.h"

#include <string.h>

CommandResult command_identify(Server *server, Client *client, char *params) {
    char *first;
    char *second;
    const char *account;
    const char *password;
    int was_identified;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "IDENTIFY");
        return COMMAND_KEEP_CLIENT;
    }

    first = strtok(params, " ");
    second = strtok(NULL, "");
    if (first == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "IDENTIFY");
        return COMMAND_KEEP_CLIENT;
    }

    if (second == NULL) {
        account = client->nick;
        password = first;
    } else {
        while (*second == ' ') ++second;
        account = first;
        password = second;
    }

    was_identified = client->account_name[0] != '\0';
    if (nickserv_identify(server, client, account, password)) {
        if (!was_identified) {
            ircv3_account_notify(client);
            chanserv_sync_client_privileges(server, client);
            memoserv_notify_unread(server, client);
        }
        client_sendf(client, ":NickServ!service@%s NOTICE %s :Password accepted - you are now identified.",
                     server->config.server_name, client->nick);
    } else {
        client_sendf(client, ":NickServ!service@%s NOTICE %s :Password incorrect or account unavailable.",
                     server->config.server_name, client->nick);
    }
    return COMMAND_KEEP_CLIENT;
}
