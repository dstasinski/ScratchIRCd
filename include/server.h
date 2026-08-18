#ifndef IRCD_SERVER_H
#define IRCD_SERVER_H

#include <stddef.h>
#include <stdint.h>

#include "channel.h"
#include "client.h"
#include "dns.h"
#include "hash.h"
#include "runtime_config.h"

/**
 * Complete process-level IRC server state.
 *
 * Network listeners and connected clients are dynamic arrays.  Registered
 * nicknames and channels additionally live in RFC1459-aware hash tables for
 * fast lookup.  The DNS worker has its own pollable result descriptor and is
 * owned for the lifetime of this structure.
 */
typedef struct Server {
    ServerConfig config;                 /**< Active runtime configuration. */

    int *listen_fds;                     /**< IPv4/IPv6 listening sockets. */
    size_t listener_count;               /**< Used entries in listen_fds. */

    Client **clients;                    /**< Dynamic array of active clients. */
    size_t client_count;                 /**< Used entries in clients. */
    size_t client_capacity;              /**< Allocated client pointer slots. */
    uint64_t next_client_id;             /**< Monotonic connection identifier. */

    HashTable clients_by_nick;           /**< Registered/chosen nicknames. */
    HashTable channels_by_name;          /**< Existing channels. */
    DnsResolver dns;                     /**< Asynchronous FCrDNS worker. */
} Server;

/** Initialize tables, resolver, and IPv4/IPv6 listeners from config. */
int server_init(Server *server, const ServerConfig *config);

/** Enter the poll()-based event loop. */
void server_run(Server *server);

/** Close sockets, stop DNS, and release all server-owned resources. */
void server_destroy(Server *server);

/** Disconnect one client and remove all nickname/channel references. */
void server_disconnect(Server *server, Client *client, const char *reason);

/** Return an existing RFC1459 channel match or create a new channel. */
Channel *server_get_or_create_channel(Server *server, const char *name);

/** Delete channel if it has no remaining members. */
void server_remove_channel_if_empty(Server *server, Channel *channel);

/** Find a connected client by stable connection identifier. */
Client *server_find_client_by_id(Server *server, uint64_t id);

#endif /* IRCD_SERVER_H */
