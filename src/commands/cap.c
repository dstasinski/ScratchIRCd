/**
 * @file cap.c
 * @brief IRCv3 capability negotiation and capability bookkeeping.
 */

#include "commands.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define CAP_ADVERTISED "account-notify batch draft/chathistory sasl server-time"

typedef struct CapabilityDefinition {
    const char *name;
    ClientCapabilitySet bit;
} CapabilityDefinition;

static const CapabilityDefinition capabilities[] = {
    {"account-notify", CLIENT_CAP_ACCOUNT_NOTIFY},
    {"batch", CLIENT_CAP_BATCH},
    {"draft/chathistory", CLIENT_CAP_CHATHISTORY},
    {"sasl", CLIENT_CAP_SASL},
    {"server-time", CLIENT_CAP_SERVER_TIME}
};

static void send_cap(Server *server, Client *client,
                     const char *subcommand, const char *caps) {
    client_sendf(client, ":%s CAP %s %s :%s",
                 server->config.server_name,
                 command_reply_nick(client), subcommand,
                 caps != NULL ? caps : "");
}

static ClientCapabilitySet capability_bit(const char *name) {
    size_t i;
    for (i = 0U; i < sizeof(capabilities) / sizeof(capabilities[0]); ++i)
        if (strcmp(name, capabilities[i].name) == 0) return capabilities[i].bit;
    return 0U;
}

static void enabled_capabilities(const Client *client, char *out, size_t out_size) {
    size_t i;
    size_t used = 0U;
    out[0] = '\0';
    for (i = 0U; i < sizeof(capabilities) / sizeof(capabilities[0]); ++i) {
        int written;
        if ((client->capabilities & capabilities[i].bit) == 0U) continue;
        written = snprintf(out + used, out_size - used, "%s%s",
                           used != 0U ? " " : "", capabilities[i].name);
        if (written < 0 || (size_t)written >= out_size - used) return;
        used += (size_t)written;
    }
}

/** Apply a CAP REQ atomically: an unknown capability causes a NAK and no changes. */
static int apply_request(Client *client, char *request) {
    ClientCapabilitySet add = 0U;
    ClientCapabilitySet remove = 0U;
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *save = NULL;
    char *token;

    if (request == NULL || *request == '\0') return 0;
    (void)snprintf(copy, sizeof(copy), "%s", request);
    for (token = strtok_r(copy, " ", &save); token != NULL;
         token = strtok_r(NULL, " ", &save)) {
        int disable = token[0] == '-';
        const char *name = disable ? token + 1 : token;
        ClientCapabilitySet bit = capability_bit(name);
        if (*name == '\0' || bit == 0U) return 0;
        if (disable) remove |= bit;
        else add |= bit;
    }
    client->capabilities |= add;
    client->capabilities &= ~remove;
    return 1;
}

CommandResult command_cap(Server *server, Client *client, char *params) {
    char *subcommand;
    char *rest;

    if (server == NULL || client == NULL || params == NULL)
        return COMMAND_KEEP_CLIENT;
    subcommand = strtok(params, " ");
    rest = strtok(NULL, "");
    if (subcommand == NULL) return COMMAND_KEEP_CLIENT;
    if (rest != NULL) while (*rest == ' ' || *rest == ':') ++rest;

    if (strcasecmp(subcommand, "LS") == 0) {
        client->cap_negotiating = 1;
        send_cap(server, client, "LS", CAP_ADVERTISED);
    } else if (strcasecmp(subcommand, "LIST") == 0) {
        char enabled[IRCD_MESSAGE_BUFFER_SIZE];
        enabled_capabilities(client, enabled, sizeof(enabled));
        send_cap(server, client, "LIST", enabled);
    } else if (strcasecmp(subcommand, "REQ") == 0) {
        client->cap_negotiating = 1;
        if (apply_request(client, rest)) send_cap(server, client, "ACK", rest);
        else send_cap(server, client, "NAK", rest != NULL ? rest : "");
    } else if (strcasecmp(subcommand, "END") == 0) {
        client->cap_negotiating = 0;
        command_maybe_register(server, client);
    }
    return COMMAND_KEEP_CLIENT;
}
