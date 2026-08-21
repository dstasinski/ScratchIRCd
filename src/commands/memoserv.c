/**
 * @file memoserv.c
 * @brief Direct /MEMOSERV command wrapper for the virtual MemoServ service.
 */

#include "commands.h"
#include "memoserv.h"

CommandResult command_memoserv(Server *server, Client *client, char *params) {
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        char help[] = "HELP";
        memoserv_handle_message(server, client, help);
    } else {
        memoserv_handle_message(server, client, params);
    }
    return COMMAND_KEEP_CLIENT;
}
