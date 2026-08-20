#ifndef IRCD_CLIENT_H
#define IRCD_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <openssl/ssl.h>

#include "config.h"
#include "modes.h"
#include "oper.h"

struct Channel;

typedef struct ClientChannelLink {
    struct Channel *channel;
    struct ClientChannelLink *next;
} ClientChannelLink;

typedef enum ClientDnsState {
    CLIENT_DNS_NONE = 0,
    CLIENT_DNS_PENDING,
    CLIENT_DNS_VERIFIED,
    CLIENT_DNS_FAILED,
    CLIENT_DNS_TIMEOUT
} ClientDnsState;

typedef enum ClientTlsState {
    CLIENT_TLS_NONE = 0,
    CLIENT_TLS_HANDSHAKE,
    CLIENT_TLS_ESTABLISHED
} ClientTlsState;

/**
 * Complete state for one connected IRC client.
 *
 * Host identity is intentionally represented by exactly three fields:
 *
 *   real_ip
 *       The actual end-user IP address. For a direct connection this comes
 *       from the accepted socket. For an authenticated WebIRC connection it
 *       will be replaced with the real client address supplied by the trusted
 *       gateway. Security policy such as ZLINE uses this field.
 *
 *   real_host
 *       The FCrDNS-verified hostname resolved from real_ip. It remains empty
 *       when no verified hostname exists. Security policy such as KLINE may
 *       match this field as well as real_ip. It is never exposed to ordinary
 *       clients merely because it exists.
 *
 *   display_host
 *       The only hostname used in normal IRC-visible identity. Initially it is
 *       real_ip and, after successful DNS, real_host. MODE +x will replace it
 *       with a cloak; MODE +t replaces it with the assigned vhost. WHO, WHOIS,
 *       channel/user message prefixes, and channel ban masks use this field.
 */
typedef struct Client {
    uint64_t id;
    int fd;
    int address_family;
    int registered;
    int is_webirc;
    int pass_accepted;                   /**< PASS satisfied when required. */
    ClientModeSet modes;

    /** OpenSSL session/handshake state for TLS-listener clients. */
    SSL *ssl;
    ClientTlsState tls_state;
    int tls_want_write;

    /** Authorization granted by OPER or, later, an identified account. */
    OperPermissionSet oper_permissions;
    char oper_name[IRCD_OPER_NAME_MAX + 1U]; /**< Matched operator/account name. */

    char nick[IRC_NICK_MAX + 1U];
    char user[IRC_USER_MAX + 1U];
    char realname[IRC_REALNAME_MAX + 1U];
    char away[IRC_AWAY_MAX + 1U];

    char real_ip[IRC_IP_MAX + 1U];
    char real_host[IRC_HOST_MAX + 1U];
    char display_host[IRC_HOST_MAX + 1U];

    ClientDnsState dns_state;
    time_t dns_deadline;
    time_t signon_time;
    time_t last_activity;

    char quit_reason[IRC_QUIT_REASON_MAX + 1U];
    char inbuf[IRC_INPUT_BUFFER_SIZE];
    size_t inbuf_len;
    size_t channel_count;
    ClientChannelLink *channels;
} Client;

Client *client_create(int fd, uint64_t id, int address_family, const char *ip);
void client_free(void *ptr);

/**
 * Send exactly length bytes through the client's active transport.
 *
 * Plain clients use send(2); established TLS clients use SSL_write().  This
 * helper is the required path for preformatted wire messages such as channel
 * broadcasts that already contain their trailing CRLF.
 */
int client_send_raw(Client *client, const char *data, size_t length);

int client_sendf(Client *client, const char *fmt, ...);
int client_send_line(Client *client, const char *line);

#endif /* IRCD_CLIENT_H */
