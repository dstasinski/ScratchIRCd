/**
 * @file irc.c
 * @brief Minimal IRC line parser connecting the network layer to commands.
 *
 * This file intentionally contains no IRC command implementations.  It splits
 * one CRLF-stripped protocol line into a command token and the untouched
 * parameter remainder, then delegates command selection and behavior to the
 * command subsystem in src/commands/.
 */

#include "irc.h"
#include "commands.h"

#include <string.h>

int irc_handle_line(Server *server, Client *client, char *line) {
    char *command;
    char *params;
    CommandResult result;

    if (server == NULL || client == NULL || line == NULL) {
        return 0;
    }

    command = strtok(line, " ");
    params = strtok(NULL, "");
    if (command == NULL) {
        return 0;
    }

    result = command_dispatch(server, client, command, params);
    return result == COMMAND_DISCONNECT_CLIENT ? 1 : 0;
}
