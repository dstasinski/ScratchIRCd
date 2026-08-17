#ifndef IRCD_CLIENT_H
#define IRCD_CLIENT_H

#include <stddef.h>
#include "config.h"

struct Channel;

/**
 * Link from a client to one channel it has joined.
 *
 * Channel membership is represented in both directions: ChannelMember links
 * make channel broadcasts efficient, while ClientChannelLink links make
 * disconnect cleanup efficient.
 */
typedef struct ClientChannelLink {
    struct Channel *channel;            /**< Joined channel. */
    struct ClientChannelLink *next;     /**< Next channel joined by client. */
} ClientChannelLink;

/**
 * State associated with one connected IRC client.
 */
typedef struct Client {
    int fd;                             /**< Connected TCP socket. */
    int registered;                     /**< Non-zero after NICK+USER complete. */
    char nick[IRC_NICK_MAX + 1U];       /**< Current nickname. */
    char user[IRC_USER_MAX + 1U];       /**< USER/ident value. */
    char realname[IRC_REALNAME_MAX + 1U]; /**< USER trailing real name. */
    char host[IRC_HOST_MAX + 1U];       /**< Numeric peer host/address. */
    char quit_reason[IRC_QUIT_REASON_MAX + 1U]; /**< Requested QUIT reason. */
    char inbuf[IRC_INPUT_BUFFER_SIZE];  /**< Buffered incomplete network input. */
    size_t inbuf_len;                   /**< Bytes currently stored in inbuf. */
    size_t channel_count;               /**< Number of joined channels. */
    ClientChannelLink *channels;        /**< Head of joined-channel links. */
} Client;

/** Allocate and initialize a Client for a connected socket. */
Client *client_create(int fd, const char *host);

/** Free client-owned memory. The socket itself is closed by server code. */
void client_free(void *ptr);

/** Format one IRC line, append CRLF, and transmit it to client. */
int client_sendf(Client *client, const char *fmt, ...);

/** Send an already formatted IRC line, appending CRLF. */
int client_send_line(Client *client, const char *line);

#endif /* IRCD_CLIENT_H */
