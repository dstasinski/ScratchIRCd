/**
 * @file rules.c
 * @brief Implementation of the IRC RULES command.
 */

#include "commands.h"
#include "numerics.h"
#include "text_cache.h"

#include <stdio.h>
#include <string.h>

static TextFileCache rules_cache = {0};

void command_rules_reset_cache(void) {
    text_file_cache_clear(&rules_cache);
}

static void send_cached_rules_lines(Server *server, Client *client,
                                    const char *text, size_t length) {
    size_t offset = 0U;
    int overhead;
    size_t payload_limit;

    if (server == NULL || client == NULL || text == NULL) return;
    overhead = snprintf(NULL, 0, RPL_RULES,
                        server->config.server_name, client->nick, "");
    if (overhead < 0 || (size_t)overhead >= IRC_LINE_CONTENT_MAX) return;
    payload_limit = IRC_LINE_CONTENT_MAX - (size_t)overhead;

    while (offset < length) {
        size_t remaining = length - offset;
        const char *newline = memchr(text + offset, '\n', remaining);
        size_t raw_logical = newline != NULL
                                 ? (size_t)(newline - (text + offset))
                                 : remaining;
        size_t logical = raw_logical;
        size_t emitted = 0U;

        if (logical > 0U && text[offset + logical - 1U] == '\r') --logical;
        if (logical == 0U) {
            client_sendf(client, RPL_RULES,
                         server->config.server_name, client->nick, "");
        } else {
            while (emitted < logical && !client->output_overflowed) {
                char line[IRC_LINE_CONTENT_MAX + 1U];
                size_t chunk = logical - emitted;
                if (chunk > payload_limit) chunk = payload_limit;
                memcpy(line, text + offset + emitted, chunk);
                line[chunk] = '\0';
                client_sendf(client, RPL_RULES,
                             server->config.server_name, client->nick, line);
                emitted += chunk;
            }
        }
        if (client->output_overflowed) return;
        offset += raw_logical + (newline != NULL ? 1U : 0U);
    }
}

CommandResult command_rules(Server *server, Client *client, char *params) {
    const char *text;
    size_t length = 0U;
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    text = text_file_cache_get(&rules_cache, server->config.rules_file, &length);
    if (text == NULL) {
        client_sendf(client, ERR_NORULES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, RPL_RULESSTART, server->config.server_name,
                 client->nick, server->config.server_name);
    send_cached_rules_lines(server, client, text, length);
    client_sendf(client, RPL_ENDOFRULES, server->config.server_name, client->nick);
    return COMMAND_KEEP_CLIENT;
}
