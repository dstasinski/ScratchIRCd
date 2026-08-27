#include "client.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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
    rc = recv(fds[1], &byte, 1U, 0);
    assert(rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

    client_free(client);
    SSL_CTX_free(ctx);
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

    test_tls_send_waits_for_input_retry();
    return 0;
}