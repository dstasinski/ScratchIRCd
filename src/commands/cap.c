/**
 * @file cap.c
 * @brief Minimal IRCv3 CAP negotiation for SASL.
 *
 * ScratchIRCd currently advertises only the `sasl` capability. CAP negotiation
 * is permitted before registration and temporarily holds registration open
 * until CAP END is received.
 */

#include "commands.h"

#include <string.h>
#include <strings.h>

static void send_cap(Client *client, const char *subcommand, const char *caps) {
    client_sendf(client, ":%s CAP %s %s :%s",
                 IRCD_DEFAULT_SERVER_NAME,
                 command_reply_nick(client), subcommand, caps != NULL ? caps : "");
}

CommandResult command_cap(Server *server, Client *client, char *params) {
    char *subcommand;
    char *rest;
    (void)server;

    if (client == NULL || params == NULL) return COMMAND_KEEP_CLIENT;
    subcommand = strtok(params, " ");
    rest = strtok(NULL, "");
    if (subcommand == NULL) return COMMAND_KEEP_CLIENT;
    if (rest != NULL) while (*rest == ' ' || *rest == ':') ++rest;

    if (strcasecmp(subcommand, "LS") == 0) {
        client->cap_negotiating = 1;
        send_cap(client, "LS", "sasl");
    } else if (strcasecmp(subcommand, "REQ") == 0) {
        client->cap_negotiating = 1;
        if (rest != NULL && strcasecmp(rest, "sasl") == 0) {
            client->cap_sasl_enabled = 1;
            send_cap(client, "ACK", "sasl");
        } else {
            send_cap(client, "NAK", rest != NULL ? rest : "");
        }
    } else if (strcasecmp(subcommand, "END") == 0) {
        client->cap_negotiating = 0;
        command_maybe_register(server, client);
    }
    return COMMAND_KEEP_CLIENT;
}
