/**
 * @file ping.c
 * @brief Implementation of the IRC PING command.
 *
 * PING is intentionally available before registration so clients can verify
 * that the connection remains alive during registration.  PONG is a protocol
 * command rather than a numeric server reply, so its wire format is generated
 * directly here rather than through numerics.h.
 */

#include "commands.h"
#include "config.h"

CommandResult command_ping(Server *server, Client *client, char *params) {
    const char *token;

    (void)server;
    token = (params != NULL && *params != '\0') ? params : IRCD_SERVER_NAME;

    client_sendf(client, ":%s PONG %s :%s",
                 IRCD_SERVER_NAME, IRCD_SERVER_NAME, token);
    return COMMAND_KEEP_CLIENT;
}
