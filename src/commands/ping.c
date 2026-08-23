/** @file ping.c @brief IRC PING/PONG and pre-registration no-spoof response. */
#include "commands.h"
#include "nospoof.h"

CommandResult command_ping(Server *server, Client *client, char *params) {
    const char *token = (params != NULL && *params != '\0') ? params : server->config.server_name;
    client_sendf(client, ":%s PONG %s :%s", server->config.server_name,
                 server->config.server_name, token);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_pong(Server *server, Client *client, char *params) {
    (void)nospoof_handle_pong(server, client, params);
    return COMMAND_KEEP_CLIENT;
}
