/**
 * @file nickserv.c
 * @brief Direct /NICKSERV command alias for the virtual NickServ service.
 *
 * IRC clients commonly translate /NICKSERV locally into PRIVMSG NickServ.
 * ScratchIRCd also accepts NICKSERV directly so RECOVER/GHOST/RESET and the
 * rest of the service command set work consistently across clients.
 */

#include "commands.h"
#include "ircv3.h"
#include "nickserv.h"
#include "numerics.h"

CommandResult command_nickserv(Server *server, Client *client, char *params) {
    int was_identified;
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "NICKSERV");
        return COMMAND_KEEP_CLIENT;
    }
    was_identified = client->account_name[0] != '\0';
    nickserv_handle_message(server, client, params);
    if (!was_identified && client->account_name[0] != '\0')
        ircv3_account_notify(client);
    return COMMAND_KEEP_CLIENT;
}
