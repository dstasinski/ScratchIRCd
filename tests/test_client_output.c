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
    return 0;
}
