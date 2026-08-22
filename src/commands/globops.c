/**
 * @file globops.c
 * @brief Operator-to-operator GLOBOPS and LOCOPS messaging.
 *
 * ScratchIRCd never links to other servers, so both commands are scoped to
 * this daemon process. They remain distinct IRC command names so clients can
 * present them conventionally and future policy can differentiate them.
 */

#include "commands.h"
#include "message_policy.h"
#include "modes.h"
#include "numerics.h"

#include <string.h>

static CommandResult send_oper_message(Server *server, Client *client,
                                       char *params, const char *command) {
    char *text = params;
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN) ||
        !client_mode_has(client->modes, CLIENT_MODE_GLOBALS)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (text == NULL || *text == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, command);
        return COMMAND_KEEP_CLIENT;
    }
    if (*text == ':') ++text;
    oper_message_broadcast(server, client, command, text);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_globops(Server *server, Client *client, char *params) {
    return send_oper_message(server, client, params, "GLOBOPS");
}

CommandResult command_locops(Server *server, Client *client, char *params) {
    return send_oper_message(server, client, params, "LOCOPS");
}
