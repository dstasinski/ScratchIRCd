/**
 * @file ison.c
 * @brief Implementation of IRC ISON for case-insensitive online checks.
 */

#include "commands.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

static int ison_payload_fits(Server *server, Client *client, const char *payload) {
    int written;
    if (server == NULL || client == NULL || payload == NULL) return 0;
    written = snprintf(NULL, 0, RPL_ISON,
                       server->config.server_name, client->nick, payload);
    return written >= 0 && (size_t)written <= IRC_LINE_CONTENT_MAX;
}

static void send_ison_payload(Server *server, Client *client, const char *payload) {
    if (ison_payload_fits(server, client, payload))
        client_sendf(client, RPL_ISON,
                     server->config.server_name, client->nick, payload);
}

CommandResult command_ison(Server *server, Client *client, char *params) {
    char online[IRC_LINE_CONTENT_MAX + 1U] = "";
    char *nick;
    size_t used = 0U;

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL) {
        send_ison_payload(server, client, "");
        return COMMAND_KEEP_CLIENT;
    }

    for (nick = strtok(params, " "); nick != NULL; nick = strtok(NULL, " ")) {
        Client *found = hash_get(&server->clients_by_nick, nick);
        if (found != NULL && found->registered) {
            char candidate[IRC_LINE_CONTENT_MAX + 1U];
            int written = snprintf(candidate, sizeof(candidate), "%s%s%s",
                                   online, used != 0U ? " " : "", found->nick);
            if (written >= 0 && (size_t)written < sizeof(candidate) &&
                ison_payload_fits(server, client, candidate)) {
                memcpy(online, candidate, (size_t)written + 1U);
                used = (size_t)written;
            } else {
                if (used != 0U) send_ison_payload(server, client, online);
                if (!ison_payload_fits(server, client, found->nick)) continue;
                (void)snprintf(online, sizeof(online), "%s", found->nick);
                used = strlen(online);
            }
        }
    }

    send_ison_payload(server, client, online);
    return COMMAND_KEEP_CLIENT;
}
