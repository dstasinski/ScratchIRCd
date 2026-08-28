/**
 * @file motd.c
 * @brief Implementation of the IRC MOTD command.
 */

#include "commands.h"
#include "numerics.h"
#include "text_cache.h"

#include <stdio.h>
#include <string.h>

static TextFileCache motd_cache = {0};

void command_motd_reset_cache(void) {
    text_file_cache_clear(&motd_cache);
}

static void send_cached_motd_lines(Server *server, Client *client,
                                   const char *text, size_t length) {
    size_t offset = 0U;
    int overhead;
    size_t payload_limit;

    if (server == NULL || client == NULL || text == NULL) return;
    overhead = snprintf(NULL, 0, RPL_MOTD,
                        server->config.server_name, client->nick, "");
    if (overhead < 0 || (size_t)overhead >= IRC_LINE_CONTENT_MAX) return;
    payload_limit = IRC_LINE_CONTENT_MAX - (size_t)overhead;

    while (offset < length) {
        size_t remaining = length - offset;
        const char *newline = memchr(text + offset, '\n', remaining);
        size_t logical = newline != NULL ? (size_t)(newline - (text + offset)) : remaining;
        size_t emitted = 0U;

        if (logical > 0U && text[offset + logical - 1U] == '\r') --logical;
        if (logical == 0U) {
            client_sendf(client, RPL_MOTD,
                         server->config.server_name, client->nick, "");
        } else {
            while (emitted < logical && !client->output_overflowed) {
                size_t chunk = logical - emitted;
                if (chunk > payload_limit) chunk = payload_limit;
                client_sendf(client, RPL_MOTD,
                             server->config.server_name, client->nick,
                             (char[IRC_LINE_CONTENT_MAX + 1U]){0});
                {
                    char line[IRC_LINE_CONTENT_MAX + 1U];
                    memcpy(line, text + offset + emitted, chunk);
                    line[chunk] = '\0';
                    client_sendf(client, RPL_MOTD,
                                 server->config.server_name, client->nick, line);
                }
                emitted += chunk;
            }
        }
        if (client->output_overflowed) return;
        offset += (newline != NULL ? (size_t)(newline - (text + offset)) + 1U : remaining);
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
