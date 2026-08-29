#include "client.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define CLIENT_FORMAT_BUFFER_SIZE \
    (IRCV3_COMBINED_TAG_SECTION_MAX + IRC_LINE_CONTENT_MAX + 3U)
#define CLIENT_OUTPUT_FLUSH_BUDGET_BYTES 65536U

static ClientFreeHook client_free_hook = NULL;
static uint64_t labeled_batch_serial = 0U;

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
        client->output_retry_length = 0U;
        return 0;
    }
    if (client->tls_state == CLIENT_TLS_ESTABLISHED && client->ssl != NULL) {
        size_t chunk;
        int rc;
        if (client->input_retry_pending) return 0;
        (void)SSL_set_mode(client->ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        if (client->output_retry_pending) {
            if (client->output_retry_length == 0U || length < client->output_retry_length)
                return -1;
            chunk = client->output_retry_length;
        } else {
            chunk = length > (size_t)INT_MAX ? (size_t)INT_MAX : length;
        }
        rc = SSL_write(client->ssl, data, (int)chunk);
        if (rc > 0) {
            *written = (size_t)rc;
            client->output_retry_pending = 0;
            client->output_want_read = 0;
            client->output_retry_length = 0U;
            return 1;
        }
        {
            int error = SSL_get_error(client->ssl, rc);
            if (error == SSL_ERROR_WANT_READ) {
                client->output_retry_pending = 1;
                client->output_want_read = 1;
                client->output_retry_length = chunk;
                return 0;
            }
            if (error == SSL_ERROR_WANT_WRITE) {
                client->output_retry_pending = 1;
                client->output_want_read = 0;
                client->output_retry_length = chunk;
                return 0;
            }
        }
        return -1;
    }
    {
        ssize_t rc = send(client->fd, data, length, MSG_NOSIGNAL);
        client->output_retry_pending = 0;
        client->output_retry_length = 0U;
        if (rc > 0) { *written = (size_t)rc; return 1; }
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
        return -1;
    }
}

int client_flush_output(Client *client) {
    size_t flushed = 0U;
    if (client == NULL) return -1;
    while (client->outbuf_len != 0U && flushed < CLIENT_OUTPUT_FLUSH_BUDGET_BYTES) {
        size_t written = 0U;
        size_t remaining_budget = CLIENT_OUTPUT_FLUSH_BUDGET_BYTES - flushed;
        size_t attempt = client->outbuf_len < remaining_budget ? client->outbuf_len : remaining_budget;
        int rc;
        if (client->output_retry_pending && client->output_retry_length > attempt)
            attempt = client->output_retry_length;
        rc = transport_write(client, client->outbuf + client->outbuf_start, attempt, &written);
        if (rc < 0) return -1;
        if (rc == 0) return 0;
        client->outbuf_start += written;
        client->outbuf_len -= written;
        flushed += written;
    }
    if (client->outbuf_len != 0U) return 0;
    client->outbuf_start = 0U;
    client->output_retry_pending = 0;
    client->output_want_read = 0;
    client->output_retry_length = 0U;
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

    if (line == NULL || strchr(line, '\r') != NULL || strchr(line, '\n') != NULL) return 0;
    if (line[0] != '@') return strlen(line) <= IRC_LINE_CONTENT_MAX;

    separator = strchr(line, ' ');
    if (separator == NULL || separator == line + 1) return 0;
    tag_section_length = (size_t)(separator - line) + 1U;
    message_length = strlen(separator + 1);
    return tag_section_length <= IRCV3_COMBINED_TAG_SECTION_MAX &&
           message_length <= IRC_LINE_CONTENT_MAX;
}

static int line_has_tag(const char *line, const char *wanted) {
    const char *cursor;
    const char *end;
    size_t wanted_length;

    if (line == NULL || wanted == NULL || line[0] != '@') return 0;
    end = strchr(line, ' ');
    if (end == NULL) return 0;
    wanted_length = strlen(wanted);
    cursor = line + 1;
    while (cursor < end) {
        const char *token_end = memchr(cursor, ';', (size_t)(end - cursor));
        const char *equals;
        size_t key_length;
        if (token_end == NULL) token_end = end;
        equals = memchr(cursor, '=', (size_t)(token_end - cursor));
        key_length = (size_t)((equals != NULL ? equals : token_end) - cursor);
        if (key_length == wanted_length &&
            memcmp(cursor, wanted, wanted_length) == 0) return 1;
        cursor = token_end < end ? token_end + 1 : end;
    }
    return 0;
}

static int prepend_tag(char *out, size_t out_size,
                       const char *tag, const char *line) {
    int written;
    if (out == NULL || out_size == 0U || tag == NULL || *tag == '\0' ||
        line == NULL) return -1;
    if (line[0] == '@')
        written = snprintf(out, out_size, "@%s;%s", tag, line + 1);
    else
        written = snprintf(out, out_size, "@%s %s", tag, line);
    return written >= 0 && (size_t)written < out_size ? 0 : -1;
}

static void current_timestamp(char *out, size_t out_size) {
    struct timespec now;
    struct tm utc;
    char base[32];
    unsigned int millis = 0U;
    time_t seconds;

    if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
        seconds = now.tv_sec;
        millis = (unsigned int)(now.tv_nsec / 1000000L);
    } else {
        seconds = time(NULL);
    }
    if (gmtime_r(&seconds, &utc) == NULL ||
        strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &utc) != 19U) {
        (void)snprintf(out, out_size, "1970-01-01T00:00:00.000Z");
        return;
    }
    (void)snprintf(out, out_size, "%.19s.%03uZ", base, millis);
}

static int client_send_line_internal(Client *client, const char *line,
                                     int apply_labeled_response) {
    char buffer[CLIENT_FORMAT_BUFFER_SIZE];
    char tagged_once[CLIENT_FORMAT_BUFFER_SIZE];
    char tagged_twice[CLIENT_FORMAT_BUFFER_SIZE];
    char tag[IRCD_HISTORY_BATCH_ID_MAX + 16U];
    char timestamp[40];
    const char *outbound = line;
    int written;
    size_t length;
    if (client == NULL || line == NULL) return -1;

    if (apply_labeled_response && client->labeled_response_active &&
        client->labeled_response_suppressed == 0U) {
        if (!client->labeled_response_started) {
            char start[CLIENT_FORMAT_BUFFER_SIZE];
            ++labeled_batch_serial;
            if (labeled_batch_serial == 0U) ++labeled_batch_serial;
            (void)snprintf(client->labeled_response_batch,
                           sizeof(client->labeled_response_batch), "l%llx",
                           (unsigned long long)labeled_batch_serial);
            written = snprintf(start, sizeof(start),
                               "@label=%s :%s BATCH +%s labeled-response",
                               client->labeled_response_label,
                               client->labeled_response_server,
                               client->labeled_response_batch);
            if (written < 0 || (size_t)written >= sizeof(start)) return -1;
            client->labeled_response_started = 1;
            if (client_send_line_internal(client, start, 0) < 0) return -1;
        }
        /* A nested response batch (for example CHATHISTORY) already carries
         * its own batch tag. Its BATCH start/end messages are tagged with the
         * outer labeled-response ID, preserving valid nested batch structure. */
        if (!line_has_tag(outbound, "batch")) {
            written = snprintf(tag, sizeof(tag), "batch=%s",
                               client->labeled_response_batch);
            if (written < 0 || (size_t)written >= sizeof(tag) ||
                prepend_tag(tagged_once, sizeof(tagged_once), tag, outbound) != 0)
                return -1;
            outbound = tagged_once;
        }
    }

    if ((client->capabilities & CLIENT_CAP_SERVER_TIME) != 0U &&
        !line_has_tag(outbound, "time")) {
        current_timestamp(timestamp, sizeof(timestamp));
        written = snprintf(tag, sizeof(tag), "time=%s", timestamp);
        if (written >= 0 && (size_t)written < sizeof(tag) &&
            prepend_tag(tagged_twice, sizeof(tagged_twice), tag, outbound) == 0 &&
            client_line_content_fits(tagged_twice))
            outbound = tagged_twice;
        /* server-time is optional on individual messages. If an otherwise
         * valid maximum-size tag section has no room, deliver it unchanged. */
    }

    /* Untagged IRC framing allows 512 octets including CRLF. IRCv3 message
     * tags use a separate allowance. The combined tag allowance covers
     * server-added tags followed by relayed client-only tags. */
    if (!client_line_content_fits(outbound)) return -1;

    written = snprintf(buffer, sizeof(buffer), "%s\r\n", outbound);
    if (written < 0) return -1;
    length = (size_t)written;
    if (length >= sizeof(buffer)) return -1;
    if (outbound[0] != '@' && length > IRC_WIRE_LINE_MAX) return -1;
    return client_send_raw(client, buffer, length);
}

int client_send_line(Client *client, const char *line) {
    return client_send_line_internal(client, line, 1);
}

int client_sendf(Client *client, const char *fmt, ...) {
    char line[CLIENT_FORMAT_BUFFER_SIZE];
    va_list args;
    int written;
    if (client == NULL || fmt == NULL) return -1;
    va_start(args, fmt); written = vsnprintf(line, sizeof(line), fmt, args); va_end(args);
    if (written < 0) return -1;
    if ((size_t)written >= sizeof(line)) return -1;
    return client_send_line(client, line);
}

void client_labeled_response_begin(Client *client, const char *server_name,
                                   const char *label) {
    if (client == NULL || server_name == NULL || label == NULL || *label == '\0')
        return;
    client->labeled_response_active = 1;
    client->labeled_response_started = 0;
    client->labeled_response_suppressed = 0U;
    client->labeled_response_batch[0] = '\0';
    (void)snprintf(client->labeled_response_server,
                   sizeof(client->labeled_response_server), "%s", server_name);
    (void)snprintf(client->labeled_response_label,
                   sizeof(client->labeled_response_label), "%s", label);
}

void client_labeled_response_end(Client *client) {
    char line[CLIENT_FORMAT_BUFFER_SIZE];
    int written;
    if (client == NULL || !client->labeled_response_active) return;

    client->labeled_response_active = 0;
    client->labeled_response_suppressed = 0U;
    if (client->labeled_response_started) {
        written = snprintf(line, sizeof(line), ":%s BATCH -%s",
                           client->labeled_response_server,
                           client->labeled_response_batch);
    } else {
        written = snprintf(line, sizeof(line), "@label=%s :%s ACK",
                           client->labeled_response_label,
                           client->labeled_response_server);
    }
    if (written >= 0 && (size_t)written < sizeof(line))
        (void)client_send_line_internal(client, line, 0);
    client->labeled_response_started = 0;
    client->labeled_response_label[0] = '\0';
    client->labeled_response_batch[0] = '\0';
    client->labeled_response_server[0] = '\0';
}

void client_labeled_response_suppress(Client *client, int suppress) {
    if (client == NULL) return;
    if (suppress) {
        ++client->labeled_response_suppressed;
    } else if (client->labeled_response_suppressed != 0U) {
        --client->labeled_response_suppressed;
    }
}
