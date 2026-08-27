/**
 * @file quit.c
 * @brief Implementation of the IRC QUIT command.
 *
 * QUIT does not free the Client directly because the command handler executes
 * while the network layer still owns the current client pointer.  Instead it
 * stores the optional quit reason in Client.quit_reason and returns
 * COMMAND_DISCONNECT_CLIENT.  The server performs cleanup after command
 * dispatch returns, preventing use-after-free errors in input processing.
 */

#include "commands.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

CommandResult command_quit(Server *server, Client *client, char *params) {
    const char *reason = params;

    (void)server;

    if (reason != NULL && *reason == ':') {
        ++reason;
    }
    if (reason == NULL || *reason == '\0' || strlen(reason) > IRC_QUIT_REASON_MAX) {
        reason = IRC_DEFAULT_QUIT_REASON;
    }

    (void)snprintf(client->quit_reason, sizeof(client->quit_reason), "%s", reason);
    return COMMAND_DISCONNECT_CLIENT;
}
