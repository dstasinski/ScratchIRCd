#include "server.h"
#include "ban_db.h"
#include "channel_log.h"
#include "commands.h"
#include "irc.h"
#include "message_policy.h"
#include "modes.h"
#include "numerics.h"

#include <arpa/inet.h>
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

#define SERVER_ACCEPT_BUDGET_PER_LISTENER 32U
#define SERVER_DNS_RESULT_BUDGET 64U
#define SERVER_DNSBL_RESULT_BUDGET 64U
#define SERVER_DNSBL_LISTED_BUDGET 4U
#define SERVER_TLS_HANDSHAKE_BUDGET 32U
#define SERVER_INPUT_LINE_BUDGET_PER_CLIENT 32U
#define SERVER_INPUT_LINE_BUDGET_GLOBAL 512U
#define SERVER_DNSBL_REASON_ZONE_MAX \
    (IRC_QUIT_REASON_MAX - 6U - IRCD_DNSBL_NAME_MAX - 3U)
#define SERVER_DNSBL_SET_BY_NAME_MAX (IRCD_OPER_NAME_MAX - 6U)

typedef struct ServerPollClientSnapshot {
    Client *client;
    uint64_t id;
} ServerPollClientSnapshot;

typedef struct ServerConnectionIpCount {
    size_t count;
} ServerConnectionIpCount;

static void client_id_key(uint64_t id, char *buffer, size_t size) {
    (void)snprintf(buffer, size, "%llu", (unsigned long long)id);
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
}

static int canonical_numeric_ip(const char *ip, char *buffer, size_t size) {
    struct in_addr v4;
    struct in6_addr v6;
    if (ip == NULL || *ip == '\0' || buffer == NULL || size == 0U) return -1;
    if (inet_pton(AF_INET, ip, &v4) == 1)
        return inet_ntop(AF_INET, &v4, buffer, (socklen_t)size) != NULL ? 0 : -1;
    if (inet_pton(AF_INET6, ip, &v6) == 1)
        return inet_ntop(AF_INET6, &v6, buffer, (socklen_t)size) != NULL ? 0 : -1;
    return -1;
}

static int numeric_ip_equal(const char *left, const char *right) {
    char left_key[IRC_IP_MAX + 1U];
    char right_key[IRC_IP_MAX + 1U];
    return canonical_numeric_ip(left, left_key, sizeof(left_key)) == 0 &&
           canonical_numeric_ip(right, right_key, sizeof(right_key)) == 0 &&
           strcmp(left_key, right_key) == 0;
}

static ServerConnectionIpCount *connection_count_lookup(const Server *server,
                                                        const char *ip,
                                                        char *key,
                                                        size_t key_size) {
    if (server == NULL || canonical_numeric_ip(ip, key, key_size) != 0) return NULL;
    return hash_get(&server->connection_counts_by_ip, key);
}

static int connection_count_add(Server *server, const char *ip) {
    char key[IRC_IP_MAX + 1U];
    ServerConnectionIpCount *record;
    if (server == NULL || canonical_numeric_ip(ip, key, sizeof(key)) != 0) return -1;
    record = hash_get(&server->connection_counts_by_ip, key);
    if (record != NULL) {
        if (record->count == SIZE_MAX) return -1;
        ++record->count;
        return 0;
    }
    record = calloc(1U, sizeof(*record));
    if (record == NULL) return -1;
    record->count = 1U;
    if (hash_set(&server->connection_counts_by_ip, key, record) != 0) {
        free(record);
        return -1;
    }
    return 0;
}

static void connection_count_remove(Server *server, const char *ip) {
    char key[IRC_IP_MAX + 1U];
    ServerConnectionIpCount *record;
    if (server == NULL || canonical_numeric_ip(ip, key, sizeof(key)) != 0) return;
    record = hash_get(&server->connection_counts_by_ip, key);
    if (record == NULL) return;
    if (record->count > 1U) {
        --record->count;
        return;
    }
    record = hash_remove(&server->connection_counts_by_ip, key);
    free(record);
}

int server_connection_count_move(Server *server, const char *old_ip,
                                 const char *new_ip) {
    char old_key[IRC_IP_MAX + 1U];
    char new_key[IRC_IP_MAX + 1U];
    ServerConnectionIpCount *old_record;
    if (server == NULL ||
        canonical_numeric_ip(old_ip, old_key, sizeof(old_key)) != 0 ||
        canonical_numeric_ip(new_ip, new_key, sizeof(new_key)) != 0)
        return -1;
    old_record = hash_get(&server->connection_counts_by_ip, old_key);
    if (old_record == NULL || old_record->count == 0U) return -1;
    if (strcmp(old_key, new_key) == 0) return 0;
    if (connection_count_add(server, new_key) != 0) return -1;
    connection_count_remove(server, old_key);
    return 0;
}

int server_connection_limit_ip_exempt(const Server *server, const char *ip) {
    size_t index;
    if (server == NULL || ip == NULL || *ip == '\0') return 0;
    for (index = 0U; index < server->config.connection_limit_exempt_ip_count; ++index)
        if (numeric_ip_equal(ip, server->config.connection_limit_exempt_ips[index])) return 1;
    for (index = 0U; index < server->config.webirc_gateway_count; ++index)
        if (numeric_ip_equal(ip, server->config.webirc_gateways[index].ip)) return 1;
    return 0;
}

int server_connection_limit_reached(const Server *server, const char *ip,
                                    const Client *exclude) {
    char key[IRC_IP_MAX + 1U];
    ServerConnectionIpCount *record;
    size_t count;
    if (server == NULL || ip == NULL || *ip == '\0' ||
        server->config.max_connections_per_ip == 0U ||
        server_connection_limit_ip_exempt(server, ip))
        return 0;
    record = connection_count_lookup(server, ip, key, sizeof(key));
    count = record != NULL ? record->count : 0U;
    if (exclude != NULL && count > 0U && numeric_ip_equal(exclude->real_ip, key)) --count;
    return count >= server->config.max_connections_per_ip;
}

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
    for (candidate = result; candidate != NULL && server->listener_count < IRCD_MAX_LISTENERS; candidate = candidate->ai_next) {
        int fd;
        int reuse = 1;
        fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd < 0) continue;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (candidate->ai_family == AF_INET6) { int v6only = 1; (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)); }
        if (set_nonblocking(fd) != 0 || bind(fd, candidate->ai_addr, candidate->ai_addrlen) != 0 || listen(fd, IRCD_LISTEN_BACKLOG) != 0) { close(fd); continue; }
        server->listen_fds[server->listener_count] = fd;
        server->listener_tls[server->listener_count] = use_tls ? 1U : 0U;
        ++server->listener_count;
    }
    freeaddrinfo(result);
    return server->listener_count > before ? 0 : -1;
}

static int make_listeners(Server *server) {
    int tls_enabled = server->config.tls_cert_file[0] != '\0' && server->config.tls_key_file[0] != '\0';
    server->listen_fds = calloc(IRCD_MAX_LISTENERS, sizeof(*server->listen_fds));
    server->listener_tls = calloc(IRCD_MAX_LISTENERS, sizeof(*server->listener_tls));
    if (server->listen_fds == NULL || server->listener_tls == NULL) return -1;
    if (add_listeners(server, server->config.port, 0) != 0) return -1;
    if (tls_enabled && add_listeners(server, server->config.tls_port, 1) != 0) return -1;
    return 0;
}

static int init_tls(Server *server) {
    if (server->config.tls_cert_file[0] == '\0' && server->config.tls_key_file[0] == '\0') return 0;
    if (server->config.tls_cert_file[0] == '\0' || server->config.tls_key_file[0] == '\0') { fprintf(stderr, "TLS requires both tls_cert_file and tls_key_file\n"); return -1; }
    server->tls_ctx = SSL_CTX_new(TLS_server_method());
    if (server->tls_ctx == NULL) return -1;
    if (SSL_CTX_set_min_proto_version(server->tls_ctx, TLS1_2_VERSION) != 1 || SSL_CTX_use_certificate_chain_file(server->tls_ctx, server->config.tls_cert_file) != 1 || SSL_CTX_use_PrivateKey_file(server->tls_ctx, server->config.tls_key_file, SSL_FILETYPE_PEM) != 1 || SSL_CTX_check_private_key(server->tls_ctx) != 1) { fprintf(stderr, "Failed to initialize TLS certificate/key\n"); return -1; }
    SSL_CTX_set_options(server->tls_ctx, SSL_OP_NO_COMPRESSION);
    return 0;
}

static int peer_ip(const struct sockaddr_storage *address, socklen_t length, char *buffer, size_t buffer_size) {
    return getnameinfo((const struct sockaddr *)address, length, buffer, (socklen_t)buffer_size, NULL, 0U, NI_NUMERICHOST) == 0 ? 0 : -1;
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
    size_t handled = 0U;
    while (handled < SERVER_ACCEPT_BUDGET_PER_LISTENER) {
        struct sockaddr_storage address;
        socklen_t address_length = sizeof(address);
        char ip[IRC_IP_MAX + 1U];
        char id_key[32];
        Client *client;
        int fd = accept(listen_fd, (struct sockaddr *)&address, &address_length);
        if (fd < 0) { if (errno == EINTR) continue; return; }
        ++handled;
        if (peer_ip(&address, address_length, ip, sizeof(ip)) != 0) { snotice_broadcast(server, SNOTICE_FLOOD, "Connection rejected: unable to determine peer IP"); close(fd); continue; }
        if (server->client_count >= server->config.max_clients) { snotice_broadcast(server, SNOTICE_FLOOD, "Connection rejected from %s: global client limit reached (%zu/%zu)", ip, server->client_count, server->config.max_clients); close(fd); continue; }
        if (server_connection_limit_reached(server, ip, NULL)) { snotice_broadcast(server, SNOTICE_FLOOD | SNOTICE_SECURITY, "Connection rejected from %s: per-IP concurrent connection limit reached (%zu)", ip, server->config.max_connections_per_ip); close(fd); continue; }
        if (server->next_client_id == UINT64_MAX) { snotice_broadcast(server, SNOTICE_FLOOD, "Connection rejected from %s: client ID space exhausted", ip); close(fd); continue; }
        if (ensure_client_capacity(server) != 0 || set_nonblocking(fd) != 0) { snotice_broadcast(server, SNOTICE_FLOOD, "Connection rejected from %s: server resource allocation/setup failure", ip); close(fd); continue; }
        client = client_create(fd, ++server->next_client_id, ((struct sockaddr *)&address)->sa_family, ip);
        if (client == NULL) { snotice_broadcast(server, SNOTICE_FLOOD, "Connection rejected from %s: client allocation failure", ip); close(fd); continue; }
        client_set_output_limit(client, server->config.output_queue_max_bytes);
        if (use_tls) {
            if (server->tls_ctx == NULL || (client->ssl = SSL_new(server->tls_ctx)) == NULL) { snotice_broadcast(server, SNOTICE_SECURITY, "TLS connection setup failed from %s", ip); client_free(client); close(fd); continue; }
            SSL_set_fd(client->ssl, fd);
            SSL_set_accept_state(client->ssl);
            client->tls_state = CLIENT_TLS_HANDSHAKE;
        }
        client_id_key(client->id, id_key, sizeof(id_key));
        if (hash_set(&server->clients_by_id, id_key, client) != 0) {
            snotice_broadcast(server, SNOTICE_FLOOD, "Connection rejected from %s: client ID index allocation failure", ip);
            client_free(client);
            close(fd);
            continue;
        }
        if (connection_count_add(server, client->real_ip) != 0) {
            snotice_broadcast(server, SNOTICE_FLOOD, "Connection rejected from %s: per-IP index allocation failure", ip);
            (void)hash_remove(&server->clients_by_id, id_key);
            client_free(client);
            close(fd);
            continue;
        }
        server->clients[server->client_count++] = client;
        snotice_broadcast(server, SNOTICE_CONNECTIONS, "Client connection accepted: real_ip=%s transport=%s", client->real_ip, use_tls ? "TLS" : "plain");
        client->dns_state = CLIENT_DNS_PENDING;
        client->dns_deadline = time(NULL) + (time_t)server->config.dns_timeout_seconds;
        if (!use_tls) client_sendf(client, NOTICE_STARTDNS, server->config.server_name);
        if (dns_resolver_submit(&server->dns, client->id, client->address_family, client->real_ip) != 0) { client->dns_state = CLIENT_DNS_FAILED; snotice_broadcast(server, SNOTICE_DNS, "FCrDNS submission failed for %s", client->real_ip); if (!use_tls) client_sendf(client, NOTICE_FAILDNS, server->config.server_name); }
    }
}

static int advance_tls_handshake(Server *server, Client *client) {
    int rc;
    int error;
    if (client->tls_state != CLIENT_TLS_HANDSHAKE || client->ssl == NULL) return 0;
    rc = SSL_accept(client->ssl);
    if (rc == 1) {
        client->tls_state = CLIENT_TLS_ESTABLISHED;
        client->tls_want_write = 0;
        client->modes = client_mode_add(client->modes, CLIENT_MODE_SECURE);
        if (client->dns_state == CLIENT_DNS_PENDING) client_sendf(client, NOTICE_STARTDNS, server->config.server_name);
        else if (client->dns_state == CLIENT_DNS_VERIFIED) client_sendf(client, NOTICE_GOTDNS, server->config.server_name);
        else client_sendf(client, NOTICE_FAILDNS, server->config.server_name);
        return 0;
    }
    error = SSL_get_error(client->ssl, rc);
    if (error == SSL_ERROR_WANT_READ) { client->tls_want_write = 0; return 0; }
    if (error == SSL_ERROR_WANT_WRITE) { client->tls_want_write = 1; return 0; }
    snotice_broadcast(server, SNOTICE_SECURITY, "TLS handshake failed from %s", client->real_ip);
    return 1;
}

static int process_buffered_lines(Server *server, Client *client, int *pending,
                                  size_t *global_budget) {
    size_t offset = 0U;
    size_t handled = 0U;

    if (pending != NULL) *pending = 0;
    while (handled < SERVER_INPUT_LINE_BUDGET_PER_CLIENT &&
           *global_budget > 0U && offset < client->inbuf_len) {
        char *base = client->inbuf + offset;
        size_t remaining = client->inbuf_len - offset;
        char *newline = memchr(base, '\n', remaining);
        size_t raw_length, line_length, consumed;
        char line[IRC_LINE_CONTENT_MAX + 1U];
        if (newline == NULL) break;
        raw_length = (size_t)(newline - base);
        consumed = raw_length + 1U;
        line_length = raw_length;
        if (line_length > 0U && base[line_length - 1U] == '\r') --line_length;
        if (line_length > IRC_LINE_CONTENT_MAX || memchr(base, '\0', raw_length) != NULL || memchr(base, '\r', line_length) != NULL) {
            snotice_broadcast(server, SNOTICE_SECURITY, "Protocol violation from %s: malformed or overlong IRC framing", client->real_ip);
            return 1;
        }
        memcpy(line, base, line_length);
        line[line_length] = '\0';
        offset += consumed;
        ++handled;
        --*global_budget;
        if (line[0] != '\0' && irc_handle_line(server, client, line) != 0) {
            if (offset < client->inbuf_len) memmove(client->inbuf, client->inbuf + offset, client->inbuf_len - offset);
            client->inbuf_len -= offset;
            return 1;
        }
    }

    if (offset != 0U) {
        if (offset < client->inbuf_len) memmove(client->inbuf, client->inbuf + offset, client->inbuf_len - offset);
        client->inbuf_len -= offset;
    }
    if (client->inbuf_len > IRC_LINE_CONTENT_MAX + 1U && memchr(client->inbuf, '\n', client->inbuf_len) == NULL) {
        snotice_broadcast(server, SNOTICE_SECURITY, "Protocol violation from %s: overlong partial IRC line (%zu bytes)", client->real_ip, client->inbuf_len);
        return 1;
    }
    if (pending != NULL && memchr(client->inbuf, '\n', client->inbuf_len) != NULL) *pending = 1;
    return 0;
}

static int read_client(Server *server, Client *client) {
    int received;
    size_t available;
    (void)server;
    if (client->inbuf_len >= sizeof(client->inbuf) - 1U) return 1;
    available = sizeof(client->inbuf) - client->inbuf_len - 1U;
    if (client->ssl != NULL) {
        int error;
        received = SSL_read(client->ssl, client->inbuf + client->inbuf_len, (int)available);
        if (received <= 0) {
            error = SSL_get_error(client->ssl, received);
            if (error == SSL_ERROR_WANT_READ) {
                client->input_retry_pending = 1;
                client->input_want_write = 0;
                return 0;
            }
            if (error == SSL_ERROR_WANT_WRITE) {
                client->input_retry_pending = 1;
                client->input_want_write = 1;
                return 0;
            }
            return 1;
        }
        client->input_retry_pending = 0;
        client->input_want_write = 0;
    } else {
        ssize_t plain = recv(client->fd, client->inbuf + client->inbuf_len, available, 0);
        if (plain < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (plain <= 0) return 1;
        received = (int)plain;
    }
    client->inbuf_len += (size_t)received;
    return 0;
}

static int drain_buffered_client_input(Server *server, size_t *global_budget) {
    size_t initial_count;
    size_t visited = 0U;
    size_t index;
    int pending = 0;

    if (server == NULL || global_budget == NULL || server->client_count == 0U) {
        if (server != NULL) server->input_dispatch_cursor = 0U;
        return 0;
    }

    initial_count = server->client_count;
    index = server->input_dispatch_cursor % initial_count;
    while (visited < initial_count && server->client_count > 0U && *global_budget > 0U) {
        Client *client;
        int client_pending = 0;
        int disconnect;

        if (index >= server->client_count) index = 0U;
        client = server->clients[index];
        ++visited;

        if (client == NULL || client->tls_state == CLIENT_TLS_HANDSHAKE || client->inbuf_len == 0U) {
            index = (index + 1U) % server->client_count;
            continue;
        }
        disconnect = process_buffered_lines(server, client, &client_pending,
                                            global_budget);
        if (!disconnect && client->output_overflowed) {
            snotice_broadcast(server, SNOTICE_FLOOD, "SendQ exceeded for %s (nick=%s limit=%zu bytes)", client->real_ip, client->nick[0] != '\0' ? client->nick : "*", client->outbuf_limit);
            (void)snprintf(client->quit_reason, sizeof(client->quit_reason), "%s", "SendQ exceeded");
            disconnect = 1;
        }
        if (disconnect) {
            server_disconnect(server, client, client->quit_reason[0] != '\0' ? client->quit_reason : IRC_DEFAULT_QUIT_REASON);
            if (server->client_count == 0U) break;
            if (index >= server->client_count) index = 0U;
            continue;
        }
        if (client_pending) pending = 1;
        index = (index + 1U) % server->client_count;
    }

    if (server->client_count > 0U)
        server->input_dispatch_cursor = index % server->client_count;
    else
        server->input_dispatch_cursor = 0U;

    /* If the aggregate budget was consumed, an unvisited client may still
     * have complete buffered commands. Force a zero-time poll on the next turn
     * so output, resolver, accept and TLS work remain interleaved with command
     * dispatch instead of synchronously draining all clients first. */
    if (*global_budget == 0U) pending = 1;
    return pending;
}

Client *server_find_client_by_id(Server *server, uint64_t id) {
    char key[32];
    if (server == NULL) return NULL;
    client_id_key(id, key, sizeof(key));
    return hash_get(&server->clients_by_id, key);
}

static void handle_dns_result(Server *server, const DnsResult *result) {
    Client *client = server_find_client_by_id(server, result->client_id);
    if (client == NULL || client->dns_state != CLIENT_DNS_PENDING) return;
    if (result->verified && result->resolved_host[0] != '\0') {
        client->dns_state = CLIENT_DNS_VERIFIED;
        (void)snprintf(client->real_host, sizeof(client->real_host), "%s", result->resolved_host);
        snotice_broadcast(server, SNOTICE_DNS, "FCrDNS verified: %s -> %s", client->real_ip, client->real_host);
        if (!client_mode_has(client->modes, CLIENT_MODE_CLOAKED | CLIENT_MODE_VHOST)) (void)snprintf(client->display_host, sizeof(client->display_host), "%s", client->real_host);
        if (client->tls_state != CLIENT_TLS_HANDSHAKE) client_sendf(client, NOTICE_GOTDNS, server->config.server_name);
    } else {
        client->dns_state = CLIENT_DNS_FAILED; client->real_host[0] = '\0';
        snotice_broadcast(server, SNOTICE_DNS, "FCrDNS failed or did not verify for %s", client->real_ip);
        if (client->tls_state != CLIENT_TLS_HANDSHAKE) client_sendf(client, NOTICE_FAILDNS, server->config.server_name);
    }
    command_maybe_register(server, client);
}

static void drain_dns_results(Server *server) {
    DnsResult result;
    size_t handled = 0U;
    while (handled < SERVER_DNS_RESULT_BUDGET &&
           dns_resolver_read_result(&server->dns, &result) == 1) {
        handle_dns_result(server, &result);
        ++handled;
    }
}

static void handle_dnsbl_result(Server *server, const DnsblResult *result) {
    Client *client = server_find_client_by_id(server, result->client_id);
    if (client == NULL || client->dnsbl_state != CLIENT_DNSBL_PENDING) return;
    if (!result->listed) { client->dnsbl_state = CLIENT_DNSBL_CLEAR; command_maybe_register(server, client); return; }
    {
        BanDb db = {0};
        char reason[IRC_QUIT_REASON_MAX + 1U];
        char set_by[IRCD_OPER_NAME_MAX + 1U];
        const char *name = result->name[0] != '\0' ? result->name : "listed";
        const char *zone = result->zone[0] != '\0' ? result->zone : "unknown";
        client->dnsbl_state = CLIENT_DNSBL_LISTED;
        (void)snprintf(reason, sizeof(reason), "DNSBL %.*s (%.*s)",
                       (int)IRCD_DNSBL_NAME_MAX, name,
                       (int)SERVER_DNSBL_REASON_ZONE_MAX, zone);
        (void)snprintf(set_by, sizeof(set_by), "DNSBL:%.*s",
                       (int)SERVER_DNSBL_SET_BY_NAME_MAX, name);
        if (ban_db_open(&db, server->config.bans_db) == 0) {
            (void)ban_db_add_timed(&db, BAN_TYPE_ZLINE, client->real_ip, reason, set_by,
                                   server->config.zline_default_duration_seconds);
            ban_db_close(&db);
        }
        snotice_broadcast(server, SNOTICE_DNS | SNOTICE_BANS,
                          "DNSBL listed %s in %s (%s); automatic exact-IP ZLINE created for %u seconds",
                          client->real_ip, result->zone[0] != '\0' ? result->zone : "unknown",
                          result->name[0] != '\0' ? result->name : "listed",
                          server->config.zline_default_duration_seconds);
        client_sendf(client, ERR_YOUREBANNEDCREEP, server->config.server_name, client->nick[0] != '\0' ? client->nick : "*", server->config.admin_email);
        (void)snprintf(client->quit_reason, sizeof(client->quit_reason), "%s", reason);
        (void)shutdown(client->fd, SHUT_RDWR);
    }
}

static void drain_dnsbl_results(Server *server) {
    DnsblResult result;
    size_t handled = 0U;
    size_t listed = 0U;
    while (handled < SERVER_DNSBL_RESULT_BUDGET &&
           dnsbl_resolver_read_result(&server->dnsbl, &result) == 1) {
        handle_dnsbl_result(server, &result);
        ++handled;
        if (result.listed && ++listed >= SERVER_DNSBL_LISTED_BUDGET) break;
    }
}

static void expire_connection_lookups(Server *server) {
    time_t now = time(NULL);
    size_t index = 0U;
    while (index < server->client_count) {
        Client *client = server->clients[index];
        if (!client->registered && now - client->signon_time >= (time_t)server->config.registration_timeout_seconds) {
            snotice_broadcast(server, SNOTICE_FLOOD, "Registration timeout for %s (nick=%s cap=%s sasl_state=%d)", client->real_ip, client->nick[0] != '\0' ? client->nick : "*", client->cap_negotiating ? "yes" : "no", (int)client->sasl_state);
            client_sendf(client, ":%s ERROR :Registration timeout", server->config.server_name); server_disconnect(server, client, "Registration timeout"); continue;
        }
        if (server->config.nospoof_enabled && client->nospoof_started && !client->nospoof_verified && client->nospoof_deadline <= now) { snotice_broadcast(server, SNOTICE_SECURITY, "No-spoof PING timeout for %s (nick=%s)", client->real_ip, client->nick[0] != '\0' ? client->nick : "*" ); client_sendf(client, ":%s ERROR :No-spoof PING timeout", server->config.server_name); server_disconnect(server, client, "No-spoof PING timeout"); continue; }
        if (client->dns_state == CLIENT_DNS_PENDING && client->dns_deadline <= now) { client->dns_state = CLIENT_DNS_TIMEOUT; client->real_host[0] = '\0'; snotice_broadcast(server, SNOTICE_DNS, "FCrDNS timeout for %s", client->real_ip); if (client->tls_state != CLIENT_TLS_HANDSHAKE) client_sendf(client, NOTICE_FAILDNS, server->config.server_name); command_maybe_register(server, client); }
        if (client->dnsbl_state == CLIENT_DNSBL_PENDING && client->dnsbl_deadline <= now) { client->dnsbl_state = CLIENT_DNSBL_TIMEOUT; snotice_broadcast(server, SNOTICE_DNS, "DNSBL lookup timeout for %s", client->real_ip); command_maybe_register(server, client); }
        ++index;
    }
}

static void maintain_client_liveness(Server *server) {
    time_t now = time(NULL);
    size_t index = 0U;

    while (index < server->client_count) {
        Client *client = server->clients[index];

        /* Unregistered sockets have their own stricter registration timeout.
         * Liveness PINGs begin only after successful IRC registration. */
        if (!client->registered) {
            ++index;
            continue;
        }

        if (client->ping_pending) {
            if (client->ping_deadline <= now) {
                char reason[64];
                (void)snprintf(reason, sizeof(reason), "Ping Timeout: %u seconds",
                               server->config.ping_timeout_seconds);
                snotice_broadcast(server, SNOTICE_CONNECTIONS,
                                  "PING timeout for %s (nick=%s)", client->real_ip,
                                  client->nick[0] != '\0' ? client->nick : "*");
                client_sendf(client, ":%s ERROR :%s",
                             server->config.server_name, reason);
                server_disconnect(server, client, reason);
                continue;
            }
            ++index;
            continue;
        }

        if (now < client->last_activity) client->last_activity = now;
        if (now - client->last_activity >=
            (time_t)server->config.ping_interval_seconds) {
            client->ping_deadline =
                now + (time_t)server->config.ping_timeout_seconds;
            (void)snprintf(client->ping_token, sizeof(client->ping_token),
                           "%lld", (long long)client->ping_deadline);
            if (client_sendf(client, "PING :%s", client->ping_token) >= 0)
                client->ping_pending = 1;
        }
        ++index;
    }
}

int server_init(Server *server, const ServerConfig *config) {
    if (server == NULL || config == NULL) return -1;
    memset(server, 0, sizeof(*server)); server->config = *config;
    server->dns.request_read_fd = server->dns.request_write_fd = -1; server->dns.result_read_fd = server->dns.result_write_fd = -1;
    server->dnsbl.request_read_fd = server->dnsbl.request_write_fd = -1; server->dnsbl.result_read_fd = server->dnsbl.result_write_fd = -1;
    if (hash_init(&server->clients_by_id, IRCD_CLIENT_HASH_BUCKETS) != 0 || hash_init(&server->clients_by_nick, IRCD_CLIENT_HASH_BUCKETS) != 0 || hash_init(&server->channels_by_name, IRCD_CHANNEL_HASH_BUCKETS) != 0 || hash_init(&server->connection_counts_by_ip, IRCD_CLIENT_HASH_BUCKETS) != 0) { server_destroy(server); return -1; }
    if (channel_log_init(server) != 0 || init_tls(server) != 0 || dns_resolver_init(&server->dns) != 0 || dnsbl_resolver_init(&server->dnsbl) != 0 || geoip_init(&server->geoip, server->config.geoip_city_db, server->config.geoip_asn_db) != 0 || make_listeners(server) != 0) { server_destroy(server); return -1; }
    return 0;
}

Channel *server_get_or_create_channel(Server *server, const char *name) {
    Channel *channel;
    if (server == NULL || name == NULL) return NULL;
    channel = hash_get(&server->channels_by_name, name);
    if (channel != NULL) return channel;
    if (server->channel_count >= server->config.max_channels) return NULL;
    channel = channel_create(name);
    if (channel == NULL) return NULL;
    if (hash_set(&server->channels_by_name, channel->name, channel) != 0) { channel_free(channel); return NULL; }
    ++server->channel_count;
    return channel;
}

void server_remove_channel_if_empty(Server *server, Channel *channel) {
    if (server == NULL || channel == NULL || channel->member_count != 0U) return;
    if (hash_remove(&server->channels_by_name, channel->name) != NULL) {
        if (server->channel_count > 0U) --server->channel_count;
        channel_free(channel);
    }
}

static int clients_share_channel(const Client *left, const Client *right) {
    ClientChannelLink *link;
    if (left == NULL || right == NULL) return 0;
    for (link = left->channels; link != NULL; link = link->next) {
        if (link->channel != NULL && channel_has_client(link->channel, right)) return 1;
    }
    return 0;
}

void server_disconnect(Server *server, Client *client, const char *reason) {
    char quit_message[IRCD_MESSAGE_BUFFER_SIZE];
    char id_key[32];
    const char *quit_reason = reason != NULL ? reason : IRC_DEFAULT_QUIT_REASON;
    size_t index;
    int fd;
    if (server == NULL || client == NULL) return;
    snotice_broadcast(server, SNOTICE_CONNECTIONS, "Client disconnect: nick=%s user=%s display_host=%s real_ip=%s real_host=%s registered=%s reason=%s", client->nick[0] != '\0' ? client->nick : "*", client->user[0] != '\0' ? client->user : "*", client->display_host[0] != '\0' ? client->display_host : client->real_ip, client->real_ip, client->real_host[0] != '\0' ? client->real_host : "-", client->registered ? "yes" : "no", quit_reason);
    if (client->registered) {
        size_t length;
        (void)snprintf(quit_message, sizeof(quit_message), ":%s!%s@%s QUIT :%s\r\n", client->nick, client->user, client->display_host, quit_reason);
        length = strlen(quit_message);
        for (index = 0U; index < server->client_count; ++index) {
            Client *candidate = server->clients[index];
            if (candidate != NULL && candidate != client && clients_share_channel(client, candidate))
                (void)client_send_raw(candidate, quit_message, length);
        }
    }
    while (client->channels != NULL) {
        Channel *channel = client->channels->channel;
        if (client->registered) channel_log_quit(server, channel, client, quit_reason);
        channel_remove_client(channel, client);
        server_remove_channel_if_empty(server, channel);
    }
    if (client->nick[0] != '\0') (void)hash_remove(&server->clients_by_nick, client->nick);
    client_id_key(client->id, id_key, sizeof(id_key));
    (void)hash_remove(&server->clients_by_id, id_key);
    connection_count_remove(server, client->real_ip);
    fd = client->fd;
    for (index = 0U; index < server->client_count; ++index) if (server->clients[index] == client) { server->clients[index] = server->clients[server->client_count - 1U]; --server->client_count; break; }
    client_free(client); close(fd);
}

void server_run(Server *server) {
    struct pollfd *poll_fds = NULL;
    ServerPollClientSnapshot *snapshot = NULL;
    size_t poll_capacity = 0U;
    size_t snapshot_capacity = 0U;
    time_t maintenance_second = 0;
    int maintenance_second_known = 0;

    if (server == NULL) return;
    while (!server->restart_requested) {
        size_t input_line_budget = SERVER_INPUT_LINE_BUDGET_GLOBAL;
        int buffered_pending = drain_buffered_client_input(server, &input_line_budget);
        const size_t dns_index = server->listener_count;
        const size_t dnsbl_index = server->listener_count + 1U;
        const size_t client_base = server->listener_count + 2U;
        const size_t snapshot_count = server->client_count;
        const size_t total = client_base + snapshot_count;
        size_t index;
        size_t start_index;
        size_t tls_handshakes = 0U;
        int ready;

        if (server->restart_requested) break;
        if (total > poll_capacity) {
            struct pollfd *grown;
            if (total > SIZE_MAX / sizeof(*poll_fds)) {
                snotice_broadcast(server, SNOTICE_FLOOD, "Event-loop poll capacity overflow; stopping server loop");
                break;
            }
            grown = realloc(poll_fds, total * sizeof(*poll_fds));
            if (grown == NULL) {
                snotice_broadcast(server, SNOTICE_FLOOD, "Event-loop poll allocation failure; stopping server loop");
                break;
            }
            poll_fds = grown;
            poll_capacity = total;
        }
        if (snapshot_count > snapshot_capacity) {
            ServerPollClientSnapshot *grown;
            if (snapshot_count > SIZE_MAX / sizeof(*snapshot)) {
                snotice_broadcast(server, SNOTICE_FLOOD, "Event-loop snapshot capacity overflow; stopping server loop");
                break;
            }
            grown = realloc(snapshot, snapshot_count * sizeof(*snapshot));
            if (grown == NULL) {
                snotice_broadcast(server, SNOTICE_FLOOD, "Event-loop snapshot allocation failure; stopping server loop");
                break;
            }
            snapshot = grown;
            snapshot_capacity = snapshot_count;
        }

        start_index = snapshot_count > 0U ? server->tls_handshake_cursor % snapshot_count : 0U;
        for (index = 0U; index < server->listener_count; ++index) {
            poll_fds[index].fd = server->listen_fds[index];
            poll_fds[index].events = POLLIN;
            poll_fds[index].revents = 0;
        }
        poll_fds[dns_index].fd = dns_resolver_result_fd(&server->dns);
        poll_fds[dns_index].events = POLLIN;
        poll_fds[dns_index].revents = 0;
        poll_fds[dnsbl_index].fd = dnsbl_resolver_result_fd(&server->dnsbl);
        poll_fds[dnsbl_index].events = POLLIN;
        poll_fds[dnsbl_index].revents = 0;
        for (index = 0U; index < snapshot_count; ++index) {
            Client *poll_client;
            snapshot[index].client = server->clients[index];
            snapshot[index].id = server->clients[index]->id;
            poll_client = snapshot[index].client;
            poll_fds[client_base + index].fd = poll_client->fd;
            poll_fds[client_base + index].events = POLLIN;
            poll_fds[client_base + index].revents = 0;
            if (poll_client->tls_state == CLIENT_TLS_HANDSHAKE && poll_client->tls_want_write) {
                poll_fds[client_base + index].events |= POLLOUT;
            } else if (poll_client->tls_state == CLIENT_TLS_ESTABLISHED && poll_client->ssl != NULL && poll_client->input_retry_pending) {
                if (poll_client->input_want_write) poll_fds[client_base + index].events = POLLOUT;
            } else if (poll_client->tls_state == CLIENT_TLS_ESTABLISHED && poll_client->ssl != NULL && poll_client->output_retry_pending) {
                poll_fds[client_base + index].events = poll_client->output_want_read ? POLLIN : POLLOUT;
            } else if (poll_client->tls_state != CLIENT_TLS_HANDSHAKE && client_output_pending(poll_client) && !poll_client->output_want_read) {
                poll_fds[client_base + index].events |= POLLOUT;
            }
        }
        ready = poll(poll_fds, (nfds_t)total, buffered_pending ? 0 : 1000);
        if (ready < 0 && errno != EINTR) {
            perror("poll");
            break;
        }
        if (ready > 0) {
            for (index = 0U; index < server->listener_count; ++index) if ((poll_fds[index].revents & POLLIN) != 0) accept_clients(server, server->listen_fds[index], server->listener_tls[index] != 0U);
            if ((poll_fds[dns_index].revents & POLLIN) != 0) drain_dns_results(server);
            if ((poll_fds[dnsbl_index].revents & POLLIN) != 0) drain_dnsbl_results(server);
            for (index = 0U; index < snapshot_count; ++index) {
                size_t client_index = (start_index + index) % snapshot_count;
                const ServerPollClientSnapshot entry = snapshot[client_index];
                Client *client = server_find_client_by_id(server, entry.id);
                short events = poll_fds[client_base + client_index].revents;
                int disconnect = 0;
                if (client == NULL || client != entry.client) continue;
                if (client->output_overflowed) {
                    snotice_broadcast(server, SNOTICE_FLOOD, "SendQ exceeded for %s (nick=%s limit=%zu bytes)", client->real_ip, client->nick[0] != '\0' ? client->nick : "*", client->outbuf_limit);
                    server_disconnect(server, client, "SendQ exceeded");
                    continue;
                }
                if (events == 0) continue;
                if ((events & (POLLERR | POLLHUP | POLLNVAL)) != 0) disconnect = 1;
                else if (client->tls_state == CLIENT_TLS_HANDSHAKE) {
                    if ((events & (POLLIN | POLLOUT)) != 0 && tls_handshakes < SERVER_TLS_HANDSHAKE_BUDGET) {
                        ++tls_handshakes;
                        disconnect = advance_tls_handshake(server, client);
                    }
                } else if (client->tls_state == CLIENT_TLS_ESTABLISHED && client->ssl != NULL && client->input_retry_pending) {
                    if ((client->input_want_write && (events & POLLOUT) != 0) || (!client->input_want_write && (events & POLLIN) != 0))
                        disconnect = read_client(server, client);
                } else if (client->tls_state == CLIENT_TLS_ESTABLISHED && client->ssl != NULL && client->output_retry_pending) {
                    if ((client->output_want_read && (events & POLLIN) != 0) || (!client->output_want_read && (events & POLLOUT) != 0)) {
                        if (client_flush_output(client) < 0) disconnect = 1;
                    }
                } else {
                    if (client_output_pending(client) && (((events & POLLOUT) != 0) || (client->output_want_read && (events & POLLIN) != 0))) {
                        if (client_flush_output(client) < 0) disconnect = 1;
                    }
                    if (!disconnect && !client->output_retry_pending && (events & POLLIN) != 0) disconnect = read_client(server, client);
                }
                if (!disconnect && client->output_overflowed) {
                    snotice_broadcast(server, SNOTICE_FLOOD, "SendQ exceeded for %s (nick=%s limit=%zu bytes)", client->real_ip, client->nick[0] != '\0' ? client->nick : "*", client->outbuf_limit);
                    (void)snprintf(client->quit_reason, sizeof(client->quit_reason), "%s", "SendQ exceeded");
                    disconnect = 1;
                }
                if (disconnect) server_disconnect(server, client, client->quit_reason[0] != '\0' ? client->quit_reason : IRC_DEFAULT_QUIT_REASON);
            }
        }
        if (snapshot_count > 0U)
            server->tls_handshake_cursor = (start_index + SERVER_TLS_HANDSHAKE_BUDGET) % snapshot_count;
        else
            server->tls_handshake_cursor = 0U;

        /* Process newly read commands before enforcing wall-clock deadlines.
         * In particular, a PONG that arrived before its deadline must not sit
         * buffered until the next poll turn while timeout maintenance runs. */
        if (!server->restart_requested && input_line_budget > 0U)
            (void)drain_buffered_client_input(server, &input_line_budget);
        {
            time_t now = time(NULL);
            if (!maintenance_second_known || now != maintenance_second) {
                maintenance_second = now;
                maintenance_second_known = 1;
                channel_log_rotate_all(now);
                if (!server->restart_requested) expire_connection_lookups(server);
                if (!server->restart_requested) maintain_client_liveness(server);
            }
        }
    }
    free(snapshot);
    free(poll_fds);
}

void server_destroy(Server *server) {
    size_t index;
    size_t destroy_count;
    if (server == NULL) return;

    /* Full process/restart teardown does not need peer-to-peer QUIT/WATCH
     * fanout: every connection is about to be closed. Preserve the durable
     * channel-log QUIT records, then free the two sides of channel membership
     * independently. This keeps teardown proportional to clients plus their
     * bounded channel memberships instead of repeatedly unlinking and scanning
     * the shrinking live population. Set client_count to zero first so logging
     * diagnostics and client-free hooks cannot broadcast into clients already
     * being dismantled. */
    destroy_count = server->client_count;
    server->client_count = 0U;
    for (index = 0U; index < destroy_count; ++index) {
        Client *client = server->clients[index];
        ClientChannelLink *link;
        int fd;
        if (client == NULL) continue;
        if (client->registered) {
            for (link = client->channels; link != NULL; link = link->next) {
                if (link->channel != NULL)
                    channel_log_quit(server, link->channel, client,
                                     IRCD_SHUTDOWN_REASON);
            }
        }
        fd = client->fd;
        client_free(client);
        close(fd);
    }
    free(server->clients); server->clients = NULL; server->client_capacity = 0U;
    if (server->listen_fds != NULL) { for (index = 0U; index < server->listener_count; ++index) close(server->listen_fds[index]); free(server->listen_fds); server->listen_fds = NULL; }
    free(server->listener_tls); server->listener_tls = NULL; server->listener_count = 0U;
    dnsbl_resolver_destroy(&server->dnsbl); dns_resolver_destroy(&server->dns); geoip_destroy(&server->geoip);
    if (server->tls_ctx != NULL) { SSL_CTX_free(server->tls_ctx); server->tls_ctx = NULL; }
    hash_destroy(&server->channels_by_name, channel_free); server->channel_count = 0U;
    hash_destroy(&server->clients_by_nick, NULL);
    hash_destroy(&server->clients_by_id, NULL);
    hash_destroy(&server->connection_counts_by_ip, free);
}
