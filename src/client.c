#include "client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

Client *client_create(int fd, const char *host) {
    Client *client = calloc(1U, sizeof(*client));
    if (client == NULL) {
        return NULL;
    }

    client->fd = fd;
    snprintf(client->host, sizeof(client->host), "%s",
             host != NULL ? host : IRC_UNKNOWN_HOST);
    return client;
}

void client_free(void *ptr) {
    Client *client = ptr;
    ClientChannelLink *link;

    if (client == NULL) {
        return;
    }

    /* Normally empty because server_disconnect() unlinks memberships first. */
    link = client->channels;
    while (link != NULL) {
        ClientChannelLink *next = link->next;
        free(link);
        link = next;
    }
    free(client);
}

int client_send_line(Client *client, const char *line) {
    char buffer[IRCD_OUTPUT_BUFFER_SIZE];
    int written;
    size_t length;

    if (client == NULL || line == NULL) {
        return -1;
    }

    written = snprintf(buffer, sizeof(buffer), "%s\r\n", line);
    if (written < 0) {
        return -1;
    }

    length = (size_t)written;
    if (length >= sizeof(buffer)) {
        /* Preserve CRLF even if a future caller constructs an oversized line. */
        length = sizeof(buffer) - 1U;
        if (length >= 2U) {
            buffer[length - 2U] = '\r';
            buffer[length - 1U] = '\n';
        }
    }

    return (int)send(client->fd, buffer, length, MSG_NOSIGNAL);
}

int client_sendf(Client *client, const char *fmt, ...) {
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    va_list args;
    int written;

    if (client == NULL || fmt == NULL) {
        return -1;
    }

    va_start(args, fmt);
    written = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (written < 0) {
        return -1;
    }
    line[sizeof(line) - 1U] = '\0';
    return client_send_line(client, line);
}
