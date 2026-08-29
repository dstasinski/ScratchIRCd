/** @file ping.c @brief IRC PING/PONG and pre-registration no-spoof response. */
#include "commands.h"
#include "nospoof.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

static const char *pong_token(const char *params) {
    const char *token;
    const char *last;

    if (params == NULL) return NULL;
    token = params;
    while (*token == ' ') ++token;
    last = strrchr(token, ' ');
    if (last != NULL) {
        token = last + 1;
        while (*token == ' ') ++token;
    }
    if (*token == ':') ++token;
    return *token != '\0' ? token : NULL;
}

CommandResult command_pong(Server *server, Client *client, char *params) {
    const char *token;

    (void)nospoof_handle_pong(server, client, params);
    token = pong_token(params);
    if (client != NULL && client->ping_pending && token != NULL &&
        strcmp(token, client->ping_token) == 0) {
        client->ping_pending = 0;
        client->ping_deadline = 0;
        client->ping_token[0] = '\0';
        client->last_activity = time(NULL);
    }
    return COMMAND_KEEP_CLIENT;
}
