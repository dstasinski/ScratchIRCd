/**
 * @file ping.c
 * @brief Implementation of the IRC PING command.
 *
 * PING remains available before registration so clients can keep the
 * connection alive while asynchronous DNS is being resolved.
 */

#include "commands.h"

CommandResult command_ping(Server *server, Client *client, char *params) {
    const char *token = (params != NULL && *params != '\0')
                            ? params
                            : server->config.server_name;

    client_sendf(client, ":%s PONG %s :%s",
                 server->config.server_name,
                 server->config.server_name,
                 token);
    return COMMAND_KEEP_CLIENT;
}
