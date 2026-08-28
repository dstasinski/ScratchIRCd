/** @file ping.c @brief IRC PING/PONG and pre-registration no-spoof response. */
#include "commands.h"
#include "nospoof.h"

#include <stdio.h>
#include <string.h>

CommandResult command_ping(Server *server, Client *client, char *params) {
    const char *token = (params != NULL && *params != '\0') ? params : server->config.server_name;
    int overhead;
    size_t limit;
    size_t length;

    overhead = snprintf(NULL, 0, ":%s PONG %s :",
                        server->config.server_name, server->config.server_name);
    if (overhead < 0 || (size_t)overhead >= IRC_LINE_CONTENT_MAX)
        return COMMAND_KEEP_CLIENT;
    limit = IRC_LINE_CONTENT_MAX - (size_t)overhead;
    length = strlen(token);
    if (length > limit) length = limit;
    client_sendf(client, ":%s PONG %s :%.*s", server->config.server_name,
                 server->config.server_name, (int)length, token);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_pong(Server *server, Client *client, char *params) {
    (void)nospoof_handle_pong(server, client, params);
    return COMMAND_KEEP_CLIENT;
}
