#ifndef IRCD_CLIENT_H
#define IRCD_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "modes.h"

struct Channel;

/**
 * Link from a client to one channel it has joined.
 *
 * Channel privilege state is deliberately stored on ChannelMember rather than
 * here, because that structure is the canonical client/channel relationship.
 * This reverse link exists to make client disconnect cleanup efficient.
 */
typedef struct ClientChannelLink {
    struct Channel *channel;            /**< Joined channel. */
    struct ClientChannelLink *next;     /**< Next channel joined by client. */
} ClientChannelLink;

/** State of asynchronous DNS processing for one connection. */
typedef enum ClientDnsState {
    CLIENT_DNS_NONE = 0,                /**< DNS has not been queued. */
    CLIENT_DNS_PENDING,                 /**< Worker lookup is outstanding. */
    CLIENT_DNS_VERIFIED,                /**< PTR passed forward verification. */
    CLIENT_DNS_FAILED,                  /**< No trustworthy hostname found. */
    CLIENT_DNS_TIMEOUT                  /**< Registration timeout expired. */
} ClientDnsState;

/**
 * State associated with one connected IRC client.
 *
 * ScratchIRCd intentionally distinguishes the physical socket peer from the
 * effective IRC identity. For ordinary clients they are the same. An
 * authenticated WEBIRC command may replace ip/host with the end-user address
 * while socket_ip/socket_host continue to identify the gateway for auditing.
 */
typedef struct Client {
    uint64_t id;                        /**< Unique connection identifier. */
    int fd;                             /**< Connected TCP socket. */
    int address_family;                 /**< AF_INET or AF_INET6. */
    int registered;                     /**< Non-zero after registration completes. */
    int is_webirc;                      /**< Non-zero after authorized WEBIRC use. */
    ClientModeSet modes;                /**< User modes defined in modes.h. */

    char nick[IRC_NICK_MAX + 1U];       /**< Current nickname. */
    char user[IRC_USER_MAX + 1U];       /**< USER/ident value. */
    char realname[IRC_REALNAME_MAX + 1U]; /**< USER trailing real name. */
    char away[IRC_AWAY_MAX + 1U];       /**< AWAY message; empty means present. */

    char ip[IRC_IP_MAX + 1U];           /**< Effective client numeric address. */
    char host[IRC_HOST_MAX + 1U];       /**< Effective IRC-visible hostname. */
    char reverse_host[IRC_HOST_MAX + 1U]; /**< PTR result, verified or not. */
    char forward_host[IRC_HOST_MAX + 1U]; /**< Forward-confirmed hostname. */

    char socket_ip[IRC_IP_MAX + 1U];    /**< Physical TCP peer address. */
    char socket_host[IRC_HOST_MAX + 1U]; /**< Verified physical peer hostname. */

    ClientDnsState dns_state;           /**< Current asynchronous DNS state. */
    time_t dns_deadline;                /**< Registration DNS timeout deadline. */
    time_t signon_time;                 /**< TCP connection creation time. */
    time_t last_activity;               /**< Time of most recent parsed IRC command. */

    char quit_reason[IRC_QUIT_REASON_MAX + 1U]; /**< Requested QUIT reason. */
    char inbuf[IRC_INPUT_BUFFER_SIZE];  /**< Buffered incomplete network input. */
    size_t inbuf_len;                   /**< Bytes currently stored in inbuf. */
    size_t channel_count;               /**< Number of joined channels. */
    ClientChannelLink *channels;        /**< Head of joined-channel links. */
} Client;

/** Allocate and initialize a Client for a connected socket. */
Client *client_create(int fd, uint64_t id, int address_family, const char *ip);

/** Free client-owned memory. The socket itself is closed by server code. */
void client_free(void *ptr);

/** Format one IRC line, append CRLF, and transmit it to client. */
int client_sendf(Client *client, const char *fmt, ...);

/** Send an already formatted IRC line, appending CRLF. */
int client_send_line(Client *client, const char *line);

#endif /* IRCD_CLIENT_H */
