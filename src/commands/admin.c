/**
 * @file admin.c
 * @brief Implementation of IRC server-information commands.
 */

#include "commands.h"
#include "config.h"
#include "numerics.h"

CommandResult command_admin(Server *server, Client *client, char *params) {
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    if (server->config.admin_location1[0] == '\0' &&
        server->config.admin_location2[0] == '\0' &&
        server->config.admin_email[0] == '\0') {
        client_sendf(client, ERR_NOADMININFO,
                     server->config.server_name, client->nick,
                     server->config.server_name);
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, RPL_ADMINME,
                 server->config.server_name, client->nick,
                 server->config.server_name);
    client_sendf(client, RPL_ADMINLOC1,
                 server->config.server_name, client->nick,
                 server->config.admin_location1);
    client_sendf(client, RPL_ADMINLOC2,
                 server->config.server_name, client->nick,
                 server->config.admin_location2);
    client_sendf(client, RPL_ADMINEMAIL,
                 server->config.server_name, client->nick,
                 server->config.admin_email);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_version(Server *server, Client *client, char *params) {
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, ":%s 351 %s %s %s :single-server C11 Linux",
                 server->config.server_name, client->nick,
                 IRCD_VERSION, server->config.server_name);
    return COMMAND_KEEP_CLIENT;
}
