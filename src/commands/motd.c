/**
 * @file motd.c
 * @brief Implementation of the IRC MOTD command.
 */

#include "commands.h"
#include "numerics.h"
#include "text_cache.h"

#include <string.h>

static TextFileCache motd_cache = {0};

void command_motd_reset_cache(void) {
    text_file_cache_clear(&motd_cache);
}

static void send_cached_motd_lines(Server *server, Client *client,
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
        client_sendf(client, RPL_MOTD, server->config.server_name, client->nick, line);
        if (chunk == logical) offset += chunk + (newline != NULL ? 1U : 0U);
        else offset += chunk;
    }
}

CommandResult command_motd(Server *server, Client *client, char *params) {
    const char *text;
    size_t length = 0U;
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    text = text_file_cache_get(&motd_cache, server->config.motd_file, &length);
    if (text == NULL) {
        client_sendf(client, ERR_NOMOTD, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, RPL_MOTDSTART, server->config.server_name,
                 client->nick, server->config.server_name);
    send_cached_motd_lines(server, client, text, length);
    client_sendf(client, RPL_ENDOFMOTD, server->config.server_name, client->nick);
    return COMMAND_KEEP_CLIENT;
}
