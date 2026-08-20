#include "client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

Client *client_create(int fd, uint64_t id, int address_family, const char *ip) {
    Client *client = calloc(1U, sizeof(*client));
    time_t now;

    if (client == NULL) {
        return NULL;
    }

    now = time(NULL);
    client->id = id;
    client->fd = fd;
    client->address_family = address_family;
    client->dns_state = CLIENT_DNS_NONE;
    client->signon_time = now;
    client->last_activity = now;

    /*
     * A newly accepted direct connection begins with the peer address as its
     * real IP and visible identity. real_host remains empty until asynchronous
     * FCrDNS succeeds. WebIRC will later replace real_ip with the trusted
     * gateway-supplied end-user address before resolving that address.
     */
    (void)snprintf(client->real_ip, sizeof(client->real_ip), "%s",
                   ip != NULL ? ip : IRC_UNKNOWN_HOST);
    client->real_host[0] = '\0';
    (void)snprintf(client->display_host, sizeof(client->display_host), "%s",
                   client->real_ip);
    return client;
}

void client_free(void *ptr) {
    Client *client = ptr;
    ClientChannelLink *link;

    if (client == NULL) {
        return;
    }

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
