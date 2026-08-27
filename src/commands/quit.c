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

static int quit_wire_fits(const Client *client, const char *reason) {
    size_t length;
    if (client == NULL || reason == NULL) return 0;
    length = 1U + strlen(client->nick) + 1U + strlen(client->user) + 1U +
             strlen(client->display_host) + sizeof(" QUIT :") - 1U +
             strlen(reason);
    return length <= IRC_LINE_CONTENT_MAX;
}

CommandResult command_quit(Server *server, Client *client, char *params) {
    const char *reason = params;

    (void)server;

    if (reason != NULL && *reason == ':') {
        ++reason;
    }
    if (reason == NULL || *reason == '\0' || strlen(reason) > IRC_QUIT_REASON_MAX ||
        (client->registered && !quit_wire_fits(client, reason))) {
        reason = IRC_DEFAULT_QUIT_REASON;
    }

    (void)snprintf(client->quit_reason, sizeof(client->quit_reason), "%s", reason);
    return COMMAND_DISCONNECT_CLIENT;
}
