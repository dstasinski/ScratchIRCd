#include "client.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

static void fill_until_queued(Client *client) {
    char block[1024];
    int attempts;
    memset(block, 'A', sizeof(block));
    for (attempts = 0; attempts < 10000 && !client_output_pending(client); ++attempts)
        assert(client_send_raw(client, block, sizeof(block)) >= 0);
    assert(client_output_pending(client));
}

static void test_flush_budget(void) {
    int fds[2];
    Client *client;
    size_t before;
    size_t consumed;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    nonblock(fds[0]);
    nonblock(fds[1]);
    client = client_create(fds[0], 5U, AF_UNIX, "127.0.0.1");
    assert(client != NULL);
    client_set_output_limit(client, 262144U);

    client->outbuf_capacity = 131072U;
    client->outbuf = malloc(client->outbuf_capacity);
    assert(client->outbuf != NULL);
    memset(client->outbuf, 'Q', client->outbuf_capacity);
    client->outbuf_start = 0U;
    client->outbuf_len = client->outbuf_capacity;
    before = client->outbuf_len;

    assert(client_flush_output(client) >= 0);
    assert(client_output_pending(client));
    consumed = before - client->outbuf_len;
    assert(consumed > 0U);
    assert(consumed <= 65536U);

    client_free(client);
    close(fds[0]);
    close(fds[1]);
}

static void test_tls_send_waits_for_input_retry(void) {
    int fds[2];
    Client *client;
    SSL_CTX *ctx;
    char byte;
    ssize_t rc;
    static const char payload[] = "queued-behind-read";

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    nonblock(fds[0]);
    nonblock(fds[1]);
    client = client_create(fds[0], 3U, AF_UNIX, "127.0.0.1");
    assert(client != NULL);
    ctx = SSL_CTX_new(TLS_method());
    assert(ctx != NULL);
    client->ssl = SSL_new(ctx);
    assert(client->ssl != NULL);
    client->tls_state = CLIENT_TLS_ESTABLISHED;
    client->input_retry_pending = 1;
    client->input_want_write = 1;

    assert(client_send_raw(client, payload, sizeof(payload) - 1U) == (int)(sizeof(payload) - 1U));
    assert(client_output_pending(client));
    assert(client->outbuf_len == sizeof(payload) - 1U);
    assert(client->output_retry_pending == 0);
    assert(client->output_retry_length == 0U);
    rc = recv(fds[1], &byte, 1U, 0);
    assert(rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

    client_free(client);
    SSL_CTX_free(ctx);
    close(fds[0]);
    close(fds[1]);
}

static void test_tagged_line_framing(void) {
    int fds[2];
    Client *client;
    char untagged[IRC_LINE_CONTENT_MAX + 2U];
    char tagged[IRCV3_SERVER_TAG_SECTION_MAX + IRC_LINE_CONTENT_MAX + 2U];
    size_t separator = IRCV3_SERVER_TAG_SECTION_MAX - 1U;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    nonblock(fds[0]);
    nonblock(fds[1]);
    client = client_create(fds[0], 4U, AF_UNIX, "127.0.0.1");
    assert(client != NULL);

    memset(untagged, 'U', IRC_LINE_CONTENT_MAX);
    untagged[IRC_LINE_CONTENT_MAX] = '\0';
    assert(client_send_line(client, untagged) >= 0);
    untagged[IRC_LINE_CONTENT_MAX] = 'U';
    untagged[IRC_LINE_CONTENT_MAX + 1U] = '\0';
    assert(client_send_line(client, untagged) < 0);

    /* No formatted caller may smuggle an extra IRC frame into one send. */
    assert(client_send_line(client, ":server NOTICE nick :safe\r\n:server NOTICE nick :injected") < 0);
    assert(client_send_line(client, ":server NOTICE nick :safe\ninjected") < 0);
    assert(client_sendf(client, ":server NOTICE nick :%s", "persisted\rtext") < 0);

    /* Exercise the exact server-tag allowance rather than only a short @time
     * prefix. The tag section includes its separating space; the ordinary IRC
     * message remains independently bounded to 510 bytes. */
    tagged[0] = '@';
    memset(tagged + 1U, 'a', separator - 1U);
    tagged[separator] = ' ';
    memset(tagged + separator + 1U, 'T', IRC_LINE_CONTENT_MAX);
    tagged[separator + 1U + IRC_LINE_CONTENT_MAX] = '\0';
    assert(separator + 1U == IRCV3_SERVER_TAG_SECTION_MAX);
    assert(strlen(tagged) == IRCV3_SERVER_TAG_SECTION_MAX + IRC_LINE_CONTENT_MAX);
    assert(client_send_line(client, tagged) >= 0);

    tagged[separator + 1U + IRC_LINE_CONTENT_MAX] = 'T';
    tagged[separator + 1U + IRC_LINE_CONTENT_MAX + 1U] = '\0';
    assert(client_send_line(client, tagged) < 0);

    client_free(client);
    close(fds[0]);
    close(fds[1]);
}

static size_t receive_available(int fd, char *buffer, size_t size) {
    size_t used = 0U;
    while (used + 1U < size) {
        ssize_t rc = recv(fd, buffer + used, size - used - 1U, 0);
        if (rc > 0) {
            used += (size_t)rc;
            continue;
        }
        assert(rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        break;
    }
    buffer[used] = '\0';
    return used;
}

static void test_server_time_and_labeled_framing(void) {
    int fds[2];
    Client *client;
    char output[8192];
    char batch_id[IRCD_HISTORY_BATCH_ID_MAX + 1U];
    char marker[128];
    char *start;
    char *end;
    size_t length;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    nonblock(fds[0]);
    nonblock(fds[1]);
    client = client_create(fds[0], 6U, AF_UNIX, "127.0.0.1");
    assert(client != NULL);
    client->capabilities = CLIENT_CAP_SERVER_TIME | CLIENT_CAP_BATCH |
                           CLIENT_CAP_LABELED_RESPONSE;

    assert(client_send_line(client, ":server NOTICE nick :timed") >= 0);
    assert(receive_available(fds[1], output, sizeof(output)) > 0U);
    assert(strncmp(output, "@time=", 6U) == 0);
    assert(strstr(output, " :server NOTICE nick :timed\r\n") != NULL);

    client_labeled_response_begin(client, "server", "unit-label");
    assert(client_send_line(client, ":server PONG server :token") >= 0);
    client_labeled_response_end(client);
    assert(receive_available(fds[1], output, sizeof(output)) > 0U);
    assert(strstr(output, "label=unit-label") != NULL);
    start = strstr(output, " BATCH +");
    assert(start != NULL);
    start += strlen(" BATCH +");
    end = strchr(start, ' ');
    assert(end != NULL);
    length = (size_t)(end - start);
    assert(length > 0U && length < sizeof(batch_id));
    memcpy(batch_id, start, length);
    batch_id[length] = '\0';
    assert(snprintf(marker, sizeof(marker), "batch=%s", batch_id) > 0);
    assert(strstr(output, marker) != NULL);
    assert(snprintf(marker, sizeof(marker), " BATCH -%s\r\n", batch_id) > 0);
    assert(strstr(output, marker) != NULL);

    client_free(client);
    close(fds[0]);
    close(fds[1]);
}

int main(void) {
    int fds[2];
    Client *client;
    char drain[8192];
    int loops;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    nonblock(fds[0]);
    nonblock(fds[1]);
    {
        int snd = 4096;
        assert(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd)) == 0);
    }
    client = client_create(fds[0], 1U, AF_UNIX, "127.0.0.1");
    assert(client != NULL);
    client_set_output_limit(client, 65536U);

    fill_until_queued(client);
    assert(!client->output_overflowed);

    for (loops = 0; loops < 10000 && client_output_pending(client); ++loops) {
        for (;;) {
            ssize_t rc = recv(fds[1], drain, sizeof(drain), 0);
            if (rc > 0) continue;
            if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            assert(rc >= 0);
            break;
        }
        assert(client_flush_output(client) >= 0);
    }
    assert(!client_output_pending(client));
    assert(!client->output_overflowed);
    client_free(client);
    close(fds[0]);
    close(fds[1]);

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    nonblock(fds[0]);
    nonblock(fds[1]);
    {
        int snd = 4096;
        assert(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd)) == 0);
    }
    client = client_create(fds[0], 2U, AF_UNIX, "127.0.0.1");
    assert(client != NULL);
    client_set_output_limit(client, 4096U);
    fill_until_queued(client);
    {
        char block[1024];
        memset(block, 'B', sizeof(block));
        for (loops = 0; loops < 100 && !client->output_overflowed; ++loops)
            (void)client_send_raw(client, block, sizeof(block));
    }
    assert(client->output_overflowed);
    assert(client->outbuf_len <= client->outbuf_limit);
    client_free(client);
    close(fds[0]);
    close(fds[1]);

    test_flush_budget();
    test_tls_send_waits_for_input_retry();
    test_tagged_line_framing();
    test_server_time_and_labeled_framing();
    return 0;
}
