/**
 * @file irc.c
 * @brief Minimal IRC line parser connecting the network layer to commands.
 */

#include "irc.h"
#include "commands.h"

#include <string.h>
#include <time.h>

int irc_handle_line(Server *server, Client *client, char *line) {
    char *command;
    char *params;
    CommandResult result;

    if (server == NULL || client == NULL || line == NULL) return 0;
    command = strtok(line, " ");
    params = strtok(NULL, "");
    if (command == NULL) return 0;
    client->last_activity = time(NULL);
    result = command_dispatch(server, client, command, params);
    return result == COMMAND_DISCONNECT_CLIENT ? 1 : 0;
}
