#ifndef IRCD_SERVER_H
#define IRCD_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <openssl/ssl.h>

#include "channel.h"
#include "client.h"
#include "dns.h"
#include "dnsbl.h"
#include "geoip.h"
#include "hash.h"
#include "history_db.h"
#include "runtime_config.h"

/** One historical nickname identity retained for WHOWAS. */
typedef struct WhowasRecord {
    char nick[IRC_NICK_MAX + 1U];
    char user[IRC_USER_MAX + 1U];
    char host[IRC_HOST_MAX + 1U];
    char realname[IRC_REALNAME_MAX + 1U];
    char server_name[IRC_HOST_MAX + 1U];
    time_t when;
} WhowasRecord;

/** Bounded read-mostly text file cached until its metadata changes. */
typedef struct ServerTextCache {
    char *data;
    size_t length;
    time_t mtime_sec;
    long mtime_nsec;
    long long file_size;
    int valid;
} ServerTextCache;

/** Complete process-level IRC server state. */
typedef struct Server {
    ServerConfig config;

    int *listen_fds;
    unsigned char *listener_tls;
    size_t listener_count;
    SSL_CTX *tls_ctx;

    Client **clients;
    size_t client_count;
    size_t client_capacity;
    uint64_t next_client_id;
    time_t started_at;

    HashTable clients_by_nick;
    HashTable channels_by_name;
    DnsResolver dns;
    DnsblResolver dnsbl;
    GeoIPContext geoip;

    /** Reused SQLite handle for high-frequency channel history reads/writes. */
    HistoryDb history_db;
    int history_db_ready;

    /** Read-mostly files cached in memory and refreshed after file changes. */
    ServerTextCache motd_cache;
    ServerTextCache rules_cache;

    /** Fixed-size in-memory ring; newest entries overwrite oldest entries. */
    WhowasRecord whowas[IRCD_WHOWAS_MAX];
    size_t whowas_next;
    size_t whowas_count;

    /** Event-loop exit requests. Restart recreates the server; shutdown exits. */
    int restart_requested;
    int shutdown_requested;
} Server;

int server_init(Server *server, const ServerConfig *config);
void server_run(Server *server);
void server_destroy(Server *server);
void server_disconnect(Server *server, Client *client, const char *reason);
Channel *server_get_or_create_channel(Server *server, const char *name);
void server_remove_channel_if_empty(Server *server, Channel *channel);
Client *server_find_client_by_id(Server *server, uint64_t id);

/** Return the reusable history DB, retrying a failed open lazily. */
HistoryDb *server_history_db(Server *server);

/** Return cached MOTD/RULES text, refreshing only when file metadata changes. */
const char *server_motd_text(Server *server, size_t *length);
const char *server_rules_text(Server *server, size_t *length);

/** Return non-zero when an IP is exempt from the concurrent per-IP limit. */
int server_connection_limit_ip_exempt(const Server *server, const char *ip);

/**
 * Return non-zero when accepting/assigning another connection to ip would
 * exceed max_connections_per_ip. exclude may be the client whose real_ip is
 * about to change during WEBIRC processing.
 */
int server_connection_limit_reached(const Server *server, const char *ip,
                                    const Client *exclude);

#endif
