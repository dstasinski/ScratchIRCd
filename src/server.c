#include "server.h"
#include "ban_db.h"
#include "channel_log.h"
#include "commands.h"
#include "irc.h"
#include "modes.h"
#include "numerics.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
}

/** Open all IPv4/IPv6 listeners for one configured port. */
static int add_listeners(Server *server, const char *port, int use_tls) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *candidate;
    const char *bind_address;
    size_t before = server->listener_count;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    bind_address = server->config.bind_address[0] != '\0' ? server->config.bind_address : NULL;

    if (getaddrinfo(bind_address, port, &hints, &result) != 0) return -1;

    for (candidate = result;
         candidate != NULL && server->listener_count < IRCD_MAX_LISTENERS;
         candidate = candidate->ai_next) {
        int fd;
        int reuse = 1;

        fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd < 0) continue;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (candidate->ai_family == AF_INET6) {
            int v6only = 1;
            (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
        }
        if (set_nonblocking(fd) != 0 ||
            bind(fd, candidate->ai_addr, candidate->ai_addrlen) != 0 ||
            listen(fd, IRCD_LISTEN_BACKLOG) != 0) {
            close(fd);
            continue;
        }
        server->listen_fds[server->listener_count] = fd;
        server->listener_tls[server->listener_count] = use_tls ? 1U : 0U;
        ++server->listener_count;
    }

    freeaddrinfo(result);
    return server->listener_count > before ? 0 : -1;
}

static int make_listeners(Server *server) {
    int tls_enabled = server->config.tls_cert_file[0] != '\0' &&
                      server->config.tls_key_file[0] != '\0';

    server->listen_fds = calloc(IRCD_MAX_LISTENERS, sizeof(*server->listen_fds));
    server->listener_tls = calloc(IRCD_MAX_LISTENERS, sizeof(*server->listener_tls));
    if (server->listen_fds == NULL || server->listener_tls == NULL) return -1;

    if (add_listeners(server, server->config.port, 0) != 0) return -1;
    if (tls_enabled && add_listeners(server, server->config.tls_port, 1) != 0) return -1;
    return 0;
}

/** Initialize a hardened shared server-side TLS context when TLS is enabled. */
static int init_tls(Server *server) {
    if (server->config.tls_cert_file[0] == '\0' &&
        server->config.tls_key_file[0] == '\0') return 0;
    if (server->config.tls_cert_file[0] == '\0' ||
        server->config.tls_key_file[0] == '\0') {
        fprintf(stderr, "TLS requires both tls_cert_file and tls_key_file\n");
        return -1;
    }

    server->tls_ctx = SSL_CTX_new(TLS_server_method());
    if (server->tls_ctx == NULL) return -1;
    if (SSL_CTX_set_min_proto_version(server->tls_ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_use_certificate_chain_file(server->tls_ctx,
                                           server->config.tls_cert_file) != 1 ||
        SSL_CTX_use_PrivateKey_file(server->tls_ctx,
                                    server->config.tls_key_file,
                                    SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(server->tls_ctx) != 1) {
        fprintf(stderr, "Failed to initialize TLS certificate/key\n");
        return -1;
    }
    SSL_CTX_set_options(server->tls_ctx, SSL_OP_NO_COMPRESSION);
    return 0;
}

static int peer_ip(const struct sockaddr_storage *address, socklen_t length,
                   char *buffer, size_t buffer_size) {
    return getnameinfo((const struct sockaddr *)address, length,
                       buffer, (socklen_t)buffer_size,
                       NULL, 0U, NI_NUMERICHOST) == 0 ? 0 : -1;
}

static int ensure_client_capacity(Server *server) {
    size_t capacity;
    Client **clients;
    if (server->client_count < server->client_capacity) return 0;
    capacity = server->client_capacity == 0U ? 16U : server->client_capacity * 2U;
    if (capacity > server->config.max_clients) capacity = server->config.max_clients;
    if (capacity <= server->client_capacity) return -1;
    clients = realloc(server->clients, capacity * sizeof(*clients));
    if (clients == NULL) return -1;
    server->clients = clients;
    server->client_capacity = capacity;
    return 0;
}

static void accept_clients(Server *server, int listen_fd, int use_tls) {
    for (;;) {
        struct sockaddr_storage address;
        socklen_t address_length = sizeof(address);
        char ip[IRC_IP_MAX + 1U];
        Client *client;
        int fd;

        fd = accept(listen_fd, (struct sockaddr *)&address, &address_length);
        if (fd < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (server->client_count >= server->config.max_clients ||
            ensure_client_capacity(server) != 0 || set_nonblocking(fd) != 0 ||
            peer_ip(&address, address_length, ip, sizeof(ip)) != 0) {
            close(fd);
            continue;
        }

        client = client_create(fd, ++server->next_client_id,
                               ((struct sockaddr *)&address)->sa_family, ip);
        if (client == NULL) {
            close(fd);
            continue;
        }

        if (use_tls) {
            if (server->tls_ctx == NULL || (client->ssl = SSL_new(server->tls_ctx)) == NULL) {
                client_free(client);
                close(fd);
                continue;
            }
            SSL_set_fd(client->ssl, fd);
            SSL_set_accept_state(client->ssl);
            client->tls_state = CLIENT_TLS_HANDSHAKE;
        }

        server->clients[server->client_count++] = client;
        client->dns_state = CLIENT_DNS_PENDING;
        client->dns_deadline = time(NULL) + (time_t)server->config.dns_timeout_seconds;

        if (!use_tls) client_sendf(client, NOTICE_STARTDNS, server->config.server_name);
        if (dns_resolver_submit(&server->dns, client->id,
                                client->address_family, client->real_ip) != 0) {
            client->dns_state = CLIENT_DNS_FAILED;
            if (!use_tls) client_sendf(client, NOTICE_FAILDNS, server->config.server_name);
        }
    }
}

/** Advance one non-blocking server-side TLS handshake. */
static int advance_tls_handshake(Server *server, Client *client) {
    int rc;
    int error;
    if (client->tls_state != CLIENT_TLS_HANDSHAKE || client->ssl == NULL) return 0;
    rc = SSL_accept(client->ssl);
    if (rc == 1) {
        client->tls_state = CLIENT_TLS_ESTABLISHED;
        client->tls_want_write = 0;
        client->modes = client_mode_add(client->modes, CLIENT_MODE_SECURE);
        if (client->dns_state == CLIENT_DNS_PENDING)
            client_sendf(client, NOTICE_STARTDNS, server->config.server_name);
        else if (client->dns_state == CLIENT_DNS_VERIFIED)
            client_sendf(client, NOTICE_GOTDNS, server->config.server_name);
        else
            client_sendf(client, NOTICE_FAILDNS, server->config.server_name);
        return 0;
    }
    error = SSL_get_error(client->ssl, rc);
    if (error == SSL_ERROR_WANT_READ) { client->tls_want_write = 0; return 0; }
    if (error == SSL_ERROR_WANT_WRITE) { client->tls_want_write = 1; return 0; }
    return 1;
}

static int process_buffered_lines(Server *server, Client *client) {
    for (;;) {
        char *newline = memchr(client->inbuf, '\n', client->inbuf_len);
        size_t line_length;
        size_t consumed;
        char line[IRC_INPUT_BUFFER_SIZE];
        if (newline == NULL) return 0;
        line_length = (size_t)(newline - client->inbuf);
        consumed = line_length + 1U;
        if (line_length > 0U && client->inbuf[line_length - 1U] == '\r') --line_length;
        memcpy(line, client->inbuf, line_length);
        line[line_length] = '\0';
        memmove(client->inbuf, client->inbuf + consumed, client->inbuf_len - consumed);
        client->inbuf_len -= consumed;
        if (line[0] != '\0' && irc_handle_line(server, client, line) != 0) return 1;
    }
}

static int read_client(Server *server, Client *client) {
    int received;
    size_t available;
    if (client->inbuf_len >= sizeof(client->inbuf) - 1U) return 1;
    available = sizeof(client->inbuf) - client->inbuf_len - 1U;

    if (client->ssl != NULL) {
        int error;
        received = SSL_read(client->ssl, client->inbuf + client->inbuf_len, (int)available);
        if (received <= 0) {
            error = SSL_get_error(client->ssl, received);
            if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) return 0;
            return 1;
        }
    } else {
        ssize_t plain = recv(client->fd, client->inbuf + client->inbuf_len, available, 0);
        if (plain < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (plain <= 0) return 1;
        received = (int)plain;
    }

    client->inbuf_len += (size_t)received;
    client->inbuf[client->inbuf_len] = '\0';
    return process_buffered_lines(server, client);
}

Client *server_find_client_by_id(Server *server, uint64_t id) {
    size_t index;
    if (server == NULL) return NULL;
    for (index = 0U; index < server->client_count; ++index)
        if (server->clients[index]->id == id) return server->clients[index];
    return NULL;
}

static void handle_dns_result(Server *server, const DnsResult *result) {
    Client *client = server_find_client_by_id(server, result->client_id);
    if (client == NULL || client->dns_state != CLIENT_DNS_PENDING) return;

    if (result->verified && result->resolved_host[0] != '\0') {
        client->dns_state = CLIENT_DNS_VERIFIED;
        (void)snprintf(client->real_host, sizeof(client->real_host), "%s", result->resolved_host);
        if (!client_mode_has(client->modes, CLIENT_MODE_CLOAKED | CLIENT_MODE_VHOST))
            (void)snprintf(client->display_host, sizeof(client->display_host), "%s", client->real_host);
        if (client->tls_state != CLIENT_TLS_HANDSHAKE)
            client_sendf(client, NOTICE_GOTDNS, server->config.server_name);
    } else {
        client->dns_state = CLIENT_DNS_FAILED;
        client->real_host[0] = '\0';
        if (client->tls_state != CLIENT_TLS_HANDSHAKE)
            client_sendf(client, NOTICE_FAILDNS, server->config.server_name);
    }
    command_maybe_register(server, client);
}

static void drain_dns_results(Server *server) {
    DnsResult result;
    while (dns_resolver_read_result(&server->dns, &result) == 1)
        handle_dns_result(server, &result);
}

/** Persist and enforce an automatic exact-IP ZLINE for a positive DNSBL hit. */
static void handle_dnsbl_result(Server *server, const DnsblResult *result) {
    Client *client = server_find_client_by_id(server, result->client_id);
    if (client == NULL || client->dnsbl_state != CLIENT_DNSBL_PENDING) return;

    if (!result->listed) {
        client->dnsbl_state = CLIENT_DNSBL_CLEAR;
        command_maybe_register(server, client);
        return;
    }

    {
        BanDb db = {0};
        char reason[IRC_QUIT_REASON_MAX + 1U];
        char set_by[IRCD_OPER_NAME_MAX + 1U];
        client->dnsbl_state = CLIENT_DNSBL_LISTED;
        (void)snprintf(reason, sizeof(reason), "DNSBL %s (%s)",
                       result->name[0] != '\0' ? result->name : "listed",
                       result->zone[0] != '\0' ? result->zone : "unknown");
        (void)snprintf(set_by, sizeof(set_by), "DNSBL:%s",
                       result->name[0] != '\0' ? result->name : "automatic");
        if (ban_db_open(&db, server->config.bans_db) == 0) {
            (void)ban_db_add(&db, BAN_TYPE_ZLINE, client->real_ip, reason, set_by);
            ban_db_close(&db);
        }
        client_sendf(client, ERR_YOUREBANNEDCREEP,
                     server->config.server_name,
                     client->nick[0] != '\0' ? client->nick : "*",
                     server->config.admin_email);
        (void)snprintf(client->quit_reason, sizeof(client->quit_reason), "%s", reason);
        (void)shutdown(client->fd, SHUT_RDWR);
    }
}

static void drain_dnsbl_results(Server *server) {
    DnsblResult result;
    while (dnsbl_resolver_read_result(&server->dnsbl, &result) == 1)
        handle_dnsbl_result(server, &result);
}

static void expire_connection_lookups(Server *server) {
    time_t now = time(NULL);
    size_t index = 0U;
    while (index < server->client_count) {
        Client *client = server->clients[index];
        if (server->config.nospoof_enabled && client->nospoof_started &&
            !client->nospoof_verified && client->nospoof_deadline <= now) {
            client_sendf(client, ":%s ERROR :No-spoof PING timeout",
                         server->config.server_name);
            server_disconnect(server, client, "No-spoof PING timeout");
            continue;
        }
        if (client->dns_state == CLIENT_DNS_PENDING && client->dns_deadline <= now) {
            client->dns_state = CLIENT_DNS_TIMEOUT;
            client->real_host[0] = '\0';
            if (client->tls_state != CLIENT_TLS_HANDSHAKE)
                client_sendf(client, NOTICE_FAILDNS, server->config.server_name);
            command_maybe_register(server, client);
        }
        if (client->dnsbl_state == CLIENT_DNSBL_PENDING &&
            client->dnsbl_deadline <= now) {
            client->dnsbl_state = CLIENT_DNSBL_TIMEOUT;
            command_maybe_register(server, client);
        }
        ++index;
    }
}

int server_init(Server *server, const ServerConfig *config) {
    if (server == NULL || config == NULL) return -1;
    memset(server, 0, sizeof(*server));
    server->config = *config;
    server->dns.request_read_fd = server->dns.request_write_fd = -1;
    server->dns.result_read_fd = server->dns.result_write_fd = -1;
    server->dnsbl.request_read_fd = server->dnsbl.request_write_fd = -1;
    server->dnsbl.result_read_fd = server->dnsbl.result_write_fd = -1;

    if (hash_init(&server->clients_by_nick, IRCD_CLIENT_HASH_BUCKETS) != 0 ||
        hash_init(&server->channels_by_name, IRCD_CHANNEL_HASH_BUCKETS) != 0) {
        server_destroy(server);
        return -1;
    }
    if (channel_log_init(server) != 0 ||
        init_tls(server) != 0 ||
        dns_resolver_init(&server->dns) != 0 ||
        dnsbl_resolver_init(&server->dnsbl) != 0 ||
        geoip_init(&server->geoip, server->config.geoip_city_db,
                   server->config.geoip_asn_db) != 0 ||
        make_listeners(server) != 0) {
        server_destroy(server);
        return -1;
    }
    return 0;
}

Channel *server_get_or_create_channel(Server *server, const char *name) {
    Channel *channel;
    if (server == NULL || name == NULL) return NULL;
    channel = hash_get(&server->channels_by_name, name);
    if (channel != NULL) return channel;
    channel = channel_create(name);
    if (channel == NULL) return NULL;
    if (hash_set(&server->channels_by_name, channel->name, channel) != 0) {
        channel_free(channel);
        return NULL;
    }
    return channel;
}

void server_remove_channel_if_empty(Server *server, Channel *channel) {
    if (server == NULL || channel == NULL || channel->member_count != 0U) return;
    (void)hash_remove(&server->channels_by_name, channel->name);
    channel_free(channel);
}

void server_disconnect(Server *server, Client *client, const char *reason) {
    char quit_message[IRCD_MESSAGE_BUFFER_SIZE];
    const char *quit_reason = reason != NULL ? reason : IRC_DEFAULT_QUIT_REASON;
    size_t index;
    int fd;

    if (server == NULL || client == NULL) return;
    if (client->registered)
        (void)snprintf(quit_message, sizeof(quit_message),
                       ":%s!%s@%s QUIT :%s\r\n",
                       client->nick, client->user, client->display_host, quit_reason);
    while (client->channels != NULL) {
        Channel *channel = client->channels->channel;
        if (client->registered) {
            channel_log_quit(server, channel, client, quit_reason);
            channel_broadcast(channel, client, quit_message);
        }
        channel_remove_client(channel, client);
        server_remove_channel_if_empty(server, channel);
    }
    if (client->nick[0] != '\0') (void)hash_remove(&server->clients_by_nick, client->nick);

    fd = client->fd;
    for (index = 0U; index < server->client_count; ++index) {
        if (server->clients[index] == client) {
            server->clients[index] = server->clients[server->client_count - 1U];
            --server->client_count;
            break;
        }
    }
    client_free(client);
    close(fd);
}

void server_run(Server *server) {
    if (server == NULL) return;

    while (!server->restart_requested) {
        const size_t dns_index = server->listener_count;
        const size_t dnsbl_index = server->listener_count + 1U;
        const size_t client_base = server->listener_count + 2U;
        const size_t total = client_base + server->client_count;
        struct pollfd *poll_fds = calloc(total, sizeof(*poll_fds));
        Client **snapshot = calloc(server->client_count, sizeof(*snapshot));
        size_t index;
        int ready;

        if (poll_fds == NULL || (server->client_count > 0U && snapshot == NULL)) {
            free(poll_fds); free(snapshot); return;
        }
        for (index = 0U; index < server->listener_count; ++index) {
            poll_fds[index].fd = server->listen_fds[index];
            poll_fds[index].events = POLLIN;
        }
        poll_fds[dns_index].fd = dns_resolver_result_fd(&server->dns);
        poll_fds[dns_index].events = POLLIN;
        poll_fds[dnsbl_index].fd = dnsbl_resolver_result_fd(&server->dnsbl);
        poll_fds[dnsbl_index].events = POLLIN;
        for (index = 0U; index < server->client_count; ++index) {
            snapshot[index] = server->clients[index];
            poll_fds[client_base + index].fd = snapshot[index]->fd;
            poll_fds[client_base + index].events = POLLIN;
            if (snapshot[index]->tls_state == CLIENT_TLS_HANDSHAKE && snapshot[index]->tls_want_write)
                poll_fds[client_base + index].events |= POLLOUT;
        }

        ready = poll(poll_fds, (nfds_t)total, 1000);
        if (ready < 0 && errno != EINTR) {
            perror("poll"); free(snapshot); free(poll_fds); return;
        }
        if (ready > 0) {
            for (index = 0U; index < server->listener_count; ++index) {
                if ((poll_fds[index].revents & POLLIN) != 0)
                    accept_clients(server, server->listen_fds[index], server->listener_tls[index] != 0U);
            }
            if ((poll_fds[dns_index].revents & POLLIN) != 0) drain_dns_results(server);
            if ((poll_fds[dnsbl_index].revents & POLLIN) != 0) drain_dnsbl_results(server);

            for (index = 0U; index + client_base < total; ++index) {
                Client *client = snapshot[index];
                short events = poll_fds[client_base + index].revents;
                int disconnect = 0;
                if (client == NULL || events == 0 || server_find_client_by_id(server, client->id) != client) continue;
                if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) disconnect = 1;
                else if (client->tls_state == CLIENT_TLS_HANDSHAKE) {
                    if ((events & (POLLIN | POLLOUT)) != 0) disconnect = advance_tls_handshake(server, client);
                } else if ((events & POLLIN) != 0) disconnect = read_client(server, client);
                if (disconnect)
                    server_disconnect(server, client,
                                      client->quit_reason[0] != '\0' ? client->quit_reason : IRC_DEFAULT_QUIT_REASON);
            }
        }
        free(snapshot); free(poll_fds);
        channel_log_rotate_all(time(NULL));
        if (!server->restart_requested) expire_connection_lookups(server);
    }
}

void server_destroy(Server *server) {
    size_t index;
    if (server == NULL) return;
    while (server->client_count > 0U)
        server_disconnect(server, server->clients[server->client_count - 1U], IRCD_SHUTDOWN_REASON);
    free(server->clients);
    server->clients = NULL;
    server->client_capacity = 0U;

    if (server->listen_fds != NULL) {
        for (index = 0U; index < server->listener_count; ++index) close(server->listen_fds[index]);
        free(server->listen_fds);
        server->listen_fds = NULL;
    }
    free(server->listener_tls);
    server->listener_tls = NULL;
    server->listener_count = 0U;

    dnsbl_resolver_destroy(&server->dnsbl);
    dns_resolver_destroy(&server->dns);
    geoip_destroy(&server->geoip);
    if (server->tls_ctx != NULL) {
        SSL_CTX_free(server->tls_ctx);
        server->tls_ctx = NULL;
    }
    hash_destroy(&server->channels_by_name, channel_free);
    hash_destroy(&server->clients_by_nick, NULL);
}
