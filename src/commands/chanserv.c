/** @file chanserv.c @brief Direct /CHANSERV command alias. */
#include "commands.h"
#include "channel_log.h"
#include "chanserv.h"
#include "numerics.h"

CommandResult command_chanserv(Server *server, Client *client, char *params) {
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "CHANSERV");
        return COMMAND_KEEP_CLIENT;
    }
    if (!channel_log_handle_chanserv(server, client, params))
        chanserv_handle_message(server, client, params);
    return COMMAND_KEEP_CLIENT;
}

/*
 * Keep the optional logger in its own module while preserving the current
 * explicit CMake source list.  This translation unit is the single owner of
 * the implementation; channel_log.c must not be compiled separately.
 */
#include "../channel_log.c"
