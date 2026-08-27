#include "client.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define IRCV3_SERVER_TAG_SECTION_MAX 4096U

static ClientFreeHook client_free_hook = NULL;

void client_set_free_hook(ClientFreeHook hook) { client_free_hook = hook; }

Client *client_create(int fd, uint64_t id, int address_family, const char *ip) {
    Client *client = calloc(1U, sizeof(*client));
    time_t now;
    if (client == NULL) return NULL;
    now = time(NULL);
    client->id = id;
    client->fd = fd;
    client->address_family = address_family;
    client->dns_state = CLIENT_DNS_NONE;
    client->tls_state = CLIENT_TLS_NONE;
    client->signon_time = now;
    client->last_activity = now;
    client->outbuf_limit = IRCD_DEFAULT_OUTPUT_QUEUE_MAX_BYTES;
    (void)snprintf(client->real_ip, sizeof(client->real_ip), "%s", ip != NULL ? ip : IRC_UNKNOWN_HOST);
    client->real_host[0] = '\0';
    (void)snprintf(client->display_host, sizeof(client->display_host), "%s", client->real_ip);
    return client;
}

void client_set_output_limit(Client *client, size_t limit) {
    if (client == NULL) return;
    client->outbuf_limit = limit;
    if (client->outbuf_len > limit) client->output_overflowed = 1;
}

int client_output_pending(const Client *client) { return client != NULL && client->outbuf_len != 0U; }

static int queue_append(Client *client, const char *data, size_t length) {
    size_t needed;
    size_t capacity;
    char *grown;
    if (length == 0U) return 0;
    if (client->outbuf_limit == 0U || length > client->outbuf_limit - client->outbuf_len) { client->output_overflowed = 1; return -1; }
    if (client->outbuf_start != 0U && client->outbuf_start + client->outbuf_len + length > client->outbuf_capacity) { memmove(client->outbuf, client->outbuf + client->outbuf_start, client->outbuf_len); client->outbuf_start = 0U; }
    needed = client->outbuf_start + client->outbuf_len + length;
    if (needed > client->outbuf_capacity) {
        capacity = client->outbuf_capacity == 0U ? 4096U : client->outbuf_capacity;
        while (capacity < needed) {
            size_t next = capacity * 2U;
            if (next < capacity || next > client->outbuf_limit) next = client->outbuf_limit;
            capacity = next;
            if (capacity < needed && capacity == client->outbuf_limit) break;
        }
        if (capacity < needed) { client->output_overflowed = 1; return -1; }
        grown = realloc(client->outbuf, capacity);
        if (grown == NULL) { client->output_overflowed = 1; return -1; }
        client->outbuf = grown;
        client->outbuf_capacity = capacity;
    }
    memcpy(client->outbuf + client->outbuf_start + client->outbuf_len, data, length);
    client->outbuf_len += length;
    return 0;
}

static int transport_write(Client *client, const char *data, size_t length, size_t *written) {
    *written = 0U;
    if (client->ssl != NULL && client->tls_state != CLIENT_TLS_ESTABLISHED) {
        client->output_retry_pending = 0;
        client->output_want_read = 0;
        return 0;
    }
    if (client->tls_state == CLIENT_TLS_ESTABLISHED && client->ssl != NULL) {
        size_t chunk;
        int rc;
        if (client->input_retry_pending) return 0;
        chunk = length > (size_t)INT_MAX ? (size_t)INT_MAX : length;
        rc = SSL_write(client->ssl, data, (int)chunk);
        if (rc > 0) {
            *written = (size_t)rc;
            client->output_retry_pending = 0;
            client->output_want_read = 0;
            return 1;
        }
        {
            int error = SSL_get_error(client->ssl, rc);
            if (error == SSL_ERROR_WANT_READ) {
                client->output_retry_pending = 1;
                client->output_want_read = 1;
                return 0;
            }
            if (error == SSL_ERROR_WANT_WRITE) {
                client->output_retry_pending = 1;
                client->output_want_read = 0;
                return 0;
            }
        }
        return -1;
    }
    {
        ssize_t rc = send(client->fd, data, length, MSG_NOSIGNAL);
        client->output_retry_pending = 0;
        if (rc > 0) { *written = (size_t)rc; return 1; }
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
        return -1;
    }
}

int client_flush_output(Client *client) {
    if (client == NULL) return -1;
    while (client->outbuf_len != 0U) {
        size_t written = 0U;
        int rc = transport_write(client, client->outbuf + client->outbuf_start, client->outbuf_len, &written);
        if (rc < 0) return -1;
        if (rc == 0) return 0;
        client->outbuf_start += written;
        client->outbuf_len -= written;
    }
    client->outbuf_start = 0U;
    client->output_retry_pending = 0;
    client->output_want_read = 0;
    return 1;
}

void client_free(void *ptr) {
    Client *client = ptr;
    ClientChannelLink *link;
    if (client == NULL) return;
    if (client_free_hook != NULL) client_free_hook(client);
    link = client->channels;
    while (link != NULL) { ClientChannelLink *next = link->next; free(link); link = next; }
    while (client->silence_list != NULL) { ClientSilenceEntry *next = client->silence_list->next; free(client->silence_list); client->silence_list = next; }
    while (client->watch_list != NULL) { ClientWatchEntry *next = client->watch_list->next; free(client->watch_list); client->watch_list = next; }
    free(client->outbuf);
    client->outbuf = NULL;
    if (client->ssl != NULL) { (void)SSL_shutdown(client->ssl); SSL_free(client->ssl); client->ssl = NULL; }
    free(client);
}

int client_send_raw(Client *client, const char *data, size_t length) {
    size_t sent = 0U;
    if (client == NULL || data == NULL) return -1;
    if (length == 0U) return 0;
    if (client->output_overflowed) return -1;
    if (client->outbuf_len != 0U) {
        if (queue_append(client, data, length) != 0) return -1;
        return length > (size_t)INT_MAX ? INT_MAX : (int)length;
    }
    while (sent < length) {
        size_t written = 0U;
        int rc = transport_write(client, data + sent, length - sent, &written);
        if (rc < 0) return -1;
        if (rc == 0) break;
        sent += written;
    }
    if (sent < length && queue_append(client, data + sent, length - sent) != 0) return -1;
    return length > (size_t)INT_MAX ? INT_MAX : (int)length;
}

static int client_line_content_fits(const char *line) {
    const char *separator;
    size_t tag_section_length;
    size_t message_length;

    if (line == NULL) return 0;
    if (line[0] != '@') return strlen(line) <= IRC_LINE_CONTENT_MAX;

    separator = strchr(line, ' ');
    if (separator == NULL || separator == line + 1) return 0;
    tag_section_length = (size_t)(separator - line) + 1U;
    message_length = strlen(separator + 1);
    return tag_section_length <= IRCV3_SERVER_TAG_SECTION_MAX &&
           message_length <= IRC_LINE_CONTENT_MAX;
}

int client_send_line(Client *client, const char *line) {
    char buffer[IRCD_OUTPUT_BUFFER_SIZE];
    int written;
    size_t length;
    if (client == NULL || line == NULL) return -1;

    /* Untagged IRC framing allows 512 octets including CRLF. IRCv3 message
     * tags use a separate allowance: validate the leading tag section and the
     * ordinary message independently rather than charging tags against the
     * classic 510-byte content budget. */
    if (!client_line_content_fits(line)) return -1;

    written = snprintf(buffer, sizeof(buffer), "%s\r\n", line);
    if (written < 0) return -1;
    length = (size_t)written;
    if (length >= sizeof(buffer)) return -1;
    if (line[0] != '@' && length > IRC_WIRE_LINE_MAX) return -1;
    return client_send_raw(client, buffer, length);
}

int client_sendf(Client *client, const char *fmt, ...) {
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    va_list args;
    int written;
    if (client == NULL || fmt == NULL) return -1;
    va_start(args, fmt); written = vsnprintf(line, sizeof(line), fmt, args); va_end(args);
    if (written < 0) return -1;
    if ((size_t)written >= sizeof(line)) return -1;
    return client_send_line(client, line);
}