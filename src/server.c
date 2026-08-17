#include "server.h"
#include "irc.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/** Create, bind, and listen on a TCP socket described by bind_addr/port. */
static int make_listener(const char *bind_addr, const char *port) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *candidate;
    int listen_fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(bind_addr, port, &hints, &result) != 0) {
        return -1;
    }

    for (candidate = result; candidate != NULL; candidate = candidate->ai_next) {
        int reuse = 1;

        listen_fd = socket(candidate->ai_family,
                           candidate->ai_socktype,
                           candidate->ai_protocol);
        if (listen_fd < 0) {
            continue;
        }

        (void)setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                         &reuse, sizeof(reuse));

        if (bind(listen_fd, candidate->ai_addr, candidate->ai_addrlen) == 0 &&
            listen(listen_fd, IRCD_LISTEN_BACKLOG) == 0) {
            break;
        }

        close(listen_fd);
        listen_fd = -1;
    }

    freeaddrinfo(result);
    return listen_fd;
}

/** Convert the accepted peer address into a numeric host string. */
static void peer_host(const struct sockaddr_storage *address, socklen_t length,
                      char *buffer, size_t buffer_size) {
    int rc = getnameinfo((const struct sockaddr *)address, length,
                         buffer, (socklen_t)buffer_size,
                         NULL, 0U, NI_NUMERICHOST);
    if (rc != 0) {
        snprintf(buffer, buffer_size, "%s", IRC_UNKNOWN_HOST);
    }
}

/** Accept one pending connection and attach it to the server client array. */
static void accept_client(Server *server) {
    struct sockaddr_storage address;
    socklen_t address_length = sizeof(address);
    char host[IRC_HOST_MAX + 1U];
    int fd;
    Client *client;

    fd = accept(server->listen_fd, (struct sockaddr *)&address, &address_length);
    if (fd < 0) {
        return;
    }

    if (server->client_count >= IRCD_MAX_CLIENTS) {
        close(fd);
        return;
    }

    peer_host(&address, address_length, host, sizeof(host));
    client = client_create(fd, host);
    if (client == NULL) {
        close(fd);
        return;
    }

    server->clients[server->client_count++] = client;
}

/**
 * Consume all complete LF-terminated IRC lines currently buffered for client.
 * Returns 1 if the command parser requests disconnection.
 */
static int process_buffered_lines(Server *server, Client *client) {
    for (;;) {
        char *newline = memchr(client->inbuf, '\n', client->inbuf_len);
        size_t line_length;
        size_t consumed;
        char line[IRC_INPUT_BUFFER_SIZE];

        if (newline == NULL) {
            return 0;
        }

        line_length = (size_t)(newline - client->inbuf);
        consumed = line_length + 1U;
        if (line_length > 0U && client->inbuf[line_length - 1U] == '\r') {
            --line_length;
        }

        memcpy(line, client->inbuf, line_length);
        line[line_length] = '\0';

        memmove(client->inbuf, client->inbuf + consumed,
                client->inbuf_len - consumed);
        client->inbuf_len -= consumed;

        if (line[0] != '\0' && irc_handle_line(server, client, line) != 0) {
            return 1;
        }
    }
}

/** Read available socket data and dispatch complete IRC input lines. */
static int read_client(Server *server, Client *client) {
    ssize_t received;
    size_t available;

    if (client->inbuf_len >= sizeof(client->inbuf) - 1U) {
        return 1;
    }

    available = sizeof(client->inbuf) - client->inbuf_len - 1U;
    received = recv(client->fd, client->inbuf + client->inbuf_len, available, 0);
    if (received <= 0) {
        return 1;
    }

    client->inbuf_len += (size_t)received;
    client->inbuf[client->inbuf_len] = '\0';
    return process_buffered_lines(server, client);
}

int server_init(Server *server, const char *bind_addr, const char *port) {
    if (server == NULL || port == NULL) {
        return -1;
    }

    memset(server, 0, sizeof(*server));
    server->listen_fd = -1;

    if (hash_init(&server->clients_by_nick, IRCD_CLIENT_HASH_BUCKETS) < 0) {
        return -1;
    }
    if (hash_init(&server->channels_by_name, IRCD_CHANNEL_HASH_BUCKETS) < 0) {
        hash_destroy(&server->clients_by_nick, NULL);
        return -1;
    }

    server->listen_fd = make_listener(bind_addr, port);
    if (server->listen_fd < 0) {
        server_destroy(server);
        return -1;
    }
    return 0;
}

Channel *server_get_or_create_channel(Server *server, const char *name) {
    Channel *channel;

    if (server == NULL || name == NULL) {
        return NULL;
    }

    channel = hash_get(&server->channels_by_name, name);
    if (channel != NULL) {
        return channel;
    }

    channel = channel_create(name);
    if (channel == NULL) {
        return NULL;
    }

    if (hash_set(&server->channels_by_name, channel->name, channel) < 0) {
        channel_free(channel);
        return NULL;
    }
    return channel;
}

void server_remove_channel_if_empty(Server *server, Channel *channel) {
    if (server == NULL || channel == NULL || channel->member_count != 0U) {
        return;
    }

    (void)hash_remove(&server->channels_by_name, channel->name);
    channel_free(channel);
}

void server_disconnect(Server *server, Client *client, const char *reason) {
    char quit_message[IRCD_MESSAGE_BUFFER_SIZE];
    const char *quit_reason = reason != NULL ? reason : IRC_DEFAULT_QUIT_REASON;
    size_t index;

    if (server == NULL || client == NULL) {
        return;
    }

    if (client->registered) {
        snprintf(quit_message, sizeof(quit_message),
                 ":%s!%s@%s QUIT :%s\r\n",
                 client->nick, client->user, client->host, quit_reason);
    }

    while (client->channels != NULL) {
        Channel *channel = client->channels->channel;
        if (client->registered) {
            channel_broadcast(channel, client, quit_message);
        }
        channel_remove_client(channel, client);
        server_remove_channel_if_empty(server, channel);
    }

    if (client->nick[0] != '\0') {
        (void)hash_remove(&server->clients_by_nick, client->nick);
    }

    close(client->fd);

    for (index = 0U; index < server->client_count; ++index) {
        if (server->clients[index] == client) {
            server->clients[index] = server->clients[server->client_count - 1U];
            server->clients[server->client_count - 1U] = NULL;
            --server->client_count;
            break;
        }
    }

    client_free(client);
}

void server_run(Server *server) {
    struct pollfd poll_fds[IRCD_MAX_CLIENTS + 1U];
    Client *poll_clients[IRCD_MAX_CLIENTS];

    if (server == NULL) {
        return;
    }

    for (;;) {
        nfds_t count;
        size_t index;
        int ready;

        memset(poll_fds, 0, sizeof(poll_fds));
        memset(poll_clients, 0, sizeof(poll_clients));

        poll_fds[0].fd = server->listen_fd;
        poll_fds[0].events = POLLIN;

        for (index = 0U; index < server->client_count; ++index) {
            poll_fds[index + 1U].fd = server->clients[index]->fd;
            poll_fds[index + 1U].events = POLLIN;
            poll_clients[index] = server->clients[index];
        }

        count = (nfds_t)(server->client_count + 1U);
        ready = poll(poll_fds, count, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            return;
        }

        if ((poll_fds[0].revents & POLLIN) != 0) {
            accept_client(server);
        }

        /* Snapshot pointers let server_disconnect() safely compact clients[]. */
        for (index = 0U; index + 1U < (size_t)count; ++index) {
            Client *client = poll_clients[index];
            short events = poll_fds[index + 1U].revents;

            if (client == NULL || events == 0) {
                continue;
            }
            if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
                ((events & POLLIN) != 0 && read_client(server, client) != 0)) {
                server_disconnect(server, client,
                                  client->quit_reason[0] != '\0'
                                      ? client->quit_reason
                                      : IRC_DEFAULT_QUIT_REASON);
            }
        }
    }
}

void server_destroy(Server *server) {
    if (server == NULL) {
        return;
    }

    while (server->client_count > 0U) {
        server_disconnect(server, server->clients[server->client_count - 1U],
                          IRCD_SHUTDOWN_REASON);
    }

    if (server->listen_fd >= 0) {
        close(server->listen_fd);
        server->listen_fd = -1;
    }

    hash_destroy(&server->channels_by_name, channel_free);
    hash_destroy(&server->clients_by_nick, NULL);
}
