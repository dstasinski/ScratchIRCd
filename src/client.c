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
    client->tls_state = CLIENT_TLS_NONE;
    client->signon_time = now;
    client->last_activity = now;

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

    if (client->ssl != NULL) {
        (void)SSL_shutdown(client->ssl);
        SSL_free(client->ssl);
        client->ssl = NULL;
    }

    link = client->channels;
    while (link != NULL) {
        ClientChannelLink *next = link->next;
        free(link);
        link = next;
    }
    free(client);
}

/** Send already-framed bytes through either plaintext TCP or established TLS. */
static int client_send_bytes(Client *client, const char *buffer, size_t length) {
    if (client->ssl != NULL) {
        int rc;
        int error;

        if (client->tls_state != CLIENT_TLS_ESTABLISHED) {
            return -1;
        }

        rc = SSL_write(client->ssl, buffer, (int)length);
        if (rc > 0) {
            return rc;
        }

        error = SSL_get_error(client->ssl, rc);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            return 0;
        }
        return -1;
    }

    return (int)send(client->fd, buffer, length, MSG_NOSIGNAL);
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

    return client_send_bytes(client, buffer, length);
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
