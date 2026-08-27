/**
 * @file rules.c
 * @brief Implementation of the IRC RULES command.
 */

#include "commands.h"
#include "numerics.h"
#include "text_cache.h"

#include <string.h>

static TextFileCache rules_cache = {0};

void command_rules_reset_cache(void) {
    text_file_cache_clear(&rules_cache);
}

static void send_cached_rules_lines(Server *server, Client *client,
                                    const char *text, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
        char line[IRCD_CONFIG_LINE_MAX];
        size_t remaining = length - offset;
        const char *newline = memchr(text + offset, '\n', remaining);
        size_t logical = newline != NULL ? (size_t)(newline - (text + offset)) : remaining;
        size_t chunk = logical < sizeof(line) - 1U ? logical : sizeof(line) - 1U;
        memcpy(line, text + offset, chunk);
        line[chunk] = '\0';
        if (chunk > 0U && line[chunk - 1U] == '\r') line[chunk - 1U] = '\0';
        client_sendf(client, RPL_RULES, server->config.server_name, client->nick, line);
        if (chunk == logical) offset += chunk + (newline != NULL ? 1U : 0U);
        else offset += chunk;
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
