/**
 * @file cap.c
 * @brief IRCv3 capability negotiation and capability bookkeeping.
 */

#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define CAP_ADVERTISED_LEGACY \
    "account-notify away-notify batch draft/chathistory extended-join " \
    "labeled-response message-tags sasl server-time"
#define CAP_ADVERTISED_302 \
    "account-notify away-notify batch draft/chathistory extended-join " \
    "labeled-response message-tags sasl=PLAIN server-time"

typedef struct CapabilityDefinition {
    const char *name;
    ClientCapabilitySet bit;
} CapabilityDefinition;

static const CapabilityDefinition capabilities[] = {
    {"account-notify", CLIENT_CAP_ACCOUNT_NOTIFY},
    {"away-notify", CLIENT_CAP_AWAY_NOTIFY},
    {"batch", CLIENT_CAP_BATCH},
    {"draft/chathistory", CLIENT_CAP_CHATHISTORY},
    {"extended-join", CLIENT_CAP_EXTENDED_JOIN},
    {"labeled-response", CLIENT_CAP_LABELED_RESPONSE},
    {"message-tags", CLIENT_CAP_MESSAGE_TAGS},
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

/** Validate a CAP REQ atomically and return its proposed capability set. */
static int requested_capabilities(const Client *client, char *request,
                                  ClientCapabilitySet *proposed_out) {
    ClientCapabilitySet proposed;
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *save = NULL;
    char *token;

    if (client == NULL || proposed_out == NULL ||
        request == NULL || *request == '\0') return 0;
    proposed = client->capabilities;
    (void)snprintf(copy, sizeof(copy), "%s", request);
    for (token = strtok_r(copy, " ", &save); token != NULL;
         token = strtok_r(NULL, " ", &save)) {
        int disable = token[0] == '-';
        const char *name = disable ? token + 1 : token;
        ClientCapabilitySet bit = capability_bit(name);
        if (*name == '\0' || bit == 0U) return 0;
        if (disable) proposed &= ~bit;
        else proposed |= bit;
    }
    if ((proposed & CLIENT_CAP_LABELED_RESPONSE) != 0U &&
        (proposed & CLIENT_CAP_BATCH) == 0U) return 0;
    *proposed_out = proposed;
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
        char *end = NULL;
        unsigned long version = rest != NULL ? strtoul(rest, &end, 10) : 0UL;
        if (rest != NULL && end != rest && *end == '\0' &&
            version > client->cap_version)
            client->cap_version = version > 302UL ? 302U : (unsigned int)version;
        client->cap_negotiating = 1;
        send_cap(server, client, "LS",
                 version >= 302UL ? CAP_ADVERTISED_302 : CAP_ADVERTISED_LEGACY);
    } else if (strcasecmp(subcommand, "LIST") == 0) {
        char enabled[IRCD_MESSAGE_BUFFER_SIZE];
        enabled_capabilities(client, enabled, sizeof(enabled));
        send_cap(server, client, "LIST", enabled);
    } else if (strcasecmp(subcommand, "REQ") == 0) {
        ClientCapabilitySet proposed;
        client->cap_negotiating = 1;
        if (requested_capabilities(client, rest, &proposed)) {
            /* Capability behavior starts only after ACK. This is especially
             * important for server-time, whose tags may not precede ACK. */
            send_cap(server, client, "ACK", rest);
            client->capabilities = proposed;
        } else {
            send_cap(server, client, "NAK", rest != NULL ? rest : "");
        }
    } else if (strcasecmp(subcommand, "END") == 0) {
        client->cap_negotiating = 0;
        command_maybe_register(server, client);
    }
    return COMMAND_KEEP_CLIENT;
}
