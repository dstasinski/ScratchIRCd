#ifndef IRCD_SERVER_H
#define IRCD_SERVER_H

#include <stddef.h>
#include "config.h"
#include "hash.h"
#include "client.h"
#include "channel.h"

/**
 * Complete process-level IRC server state.
 *
 * clients[] is the iteration array used by poll().  Registered nicknames and
 * channels additionally live in case-insensitive hash tables for constant-
 * time average lookup by protocol commands.
 */
typedef struct Server {
    int listen_fd;                                  /**< Listening TCP socket. */
    Client *clients[IRCD_MAX_CLIENTS];              /**< Active connections. */
    size_t client_count;                            /**< Used entries in clients. */
    HashTable clients_by_nick;                      /**< Registered/chosen nicks. */
    HashTable channels_by_name;                     /**< Existing channels. */
} Server;

/** Initialize hash tables and open the listening socket. */
int server_init(Server *server, const char *bind_addr, const char *port);

/** Enter the blocking poll()-based event loop. */
void server_run(Server *server);

/** Close sockets and release all server-owned resources. */
void server_destroy(Server *server);

/** Disconnect one client and remove all nickname/channel references. */
void server_disconnect(Server *server, Client *client, const char *reason);

/** Return an existing case-insensitive channel match or create a new channel. */
Channel *server_get_or_create_channel(Server *server, const char *name);

/** Delete channel if it has no remaining members. */
void server_remove_channel_if_empty(Server *server, Channel *channel);

#endif /* IRCD_SERVER_H */
