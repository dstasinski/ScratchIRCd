/**
 * @file motd.c
 * @brief Implementation of the IRC MOTD command.
 */

#include "commands.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_motd(Server *server, Client *client, char *params) {
    FILE *file;
    char line[IRCD_CONFIG_LINE_MAX];
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    file = fopen(server->config.motd_file, "r");
    if (file == NULL) {
        client_sendf(client, ERR_NOMOTD, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, RPL_MOTDSTART, server->config.server_name,
                 client->nick, server->config.server_name);
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        client_sendf(client, RPL_MOTD, server->config.server_name, client->nick, line);
    }
    fclose(file);
    client_sendf(client, RPL_ENDOFMOTD, server->config.server_name, client->nick);
    return COMMAND_KEEP_CLIENT;
}
