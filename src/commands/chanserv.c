/** @file chanserv.c @brief Direct /CHANSERV alias for the virtual ChanServ service. */
#include "commands.h"
#include "chanserv.h"
#include "numerics.h"

CommandResult command_chanserv(Server *server, Client *client, char *params) {
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "CHANSERV");
        return COMMAND_KEEP_CLIENT;
    }
    chanserv_handle_message(server, client, params);
    return COMMAND_KEEP_CLIENT;
}
