#ifndef IRCD_CLIENT_H
#define IRCD_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <openssl/ssl.h>
#include "config.h"
#include "geoip.h"
#include "modes.h"
#include "oper.h"

struct Channel;

typedef struct ClientChannelLink {
    struct Channel *channel;
    struct ClientChannelLink *next;
} ClientChannelLink;

/** One client-local SILENCE mask. */
typedef struct ClientSilenceEntry {
    char mask[IRC_CHANNEL_MASK_MAX + 1U];
    struct ClientSilenceEntry *next;
} ClientSilenceEntry;

/** One nickname on a client-local WATCH list. */
typedef struct ClientWatchEntry {
    char nick[IRC_NICK_MAX + 1U];
    struct ClientWatchEntry *next;
} ClientWatchEntry;

typedef enum ClientDnsState {
    CLIENT_DNS_NONE = 0,
    CLIENT_DNS_PENDING,
    CLIENT_DNS_VERIFIED,
    CLIENT_DNS_FAILED,
    CLIENT_DNS_TIMEOUT
} ClientDnsState;

typedef enum ClientDnsblState {
    CLIENT_DNSBL_NONE = 0,
    CLIENT_DNSBL_PENDING,
    CLIENT_DNSBL_CLEAR,
    CLIENT_DNSBL_LISTED,
    CLIENT_DNSBL_TIMEOUT,
    CLIENT_DNSBL_ERROR
} ClientDnsblState;

typedef enum ClientTlsState {
    CLIENT_TLS_NONE = 0,
    CLIENT_TLS_HANDSHAKE,
    CLIENT_TLS_ESTABLISHED
} ClientTlsState;

typedef enum ClientSaslState {
    CLIENT_SASL_NONE = 0,
    CLIENT_SASL_PLAIN_WAIT_DATA,
    CLIENT_SASL_COMPLETE,
    CLIENT_SASL_FAILED
} ClientSaslState;

/** IRCv3 capabilities currently supported by the daemon. */
typedef uint64_t ClientCapabilitySet;
#define CLIENT_CAP_SASL             (UINT64_C(1) << 0)
#define CLIENT_CAP_ACCOUNT_NOTIFY   (UINT64_C(1) << 1)
#define CLIENT_CAP_BATCH            (UINT64_C(1) << 2)
#define CLIENT_CAP_SERVER_TIME      (UINT64_C(1) << 3)
#define CLIENT_CAP_CHATHISTORY      (UINT64_C(1) << 4)

/** Gateway/audit metadata kept separate from the three client identity fields. */
typedef struct ClientWebIrc {
    int active;
    char gateway_ip[IRC_IP_MAX + 1U];
    char gateway_name[IRCD_WEBIRC_GATEWAY_NAME_MAX + 1U];
    char supplied_host[IRC_HOST_MAX + 1U];
} ClientWebIrc;

/** Complete state for one connected IRC client. */
typedef struct Client {
    uint64_t id;
    int fd;
    int address_family;
    int registered;
    int pass_accepted;
    ClientModeSet modes;

    SSL *ssl;
    ClientTlsState tls_state;
    int tls_want_write;

    OperPermissionSet oper_permissions;
    char oper_name[IRCD_OPER_NAME_MAX + 1U];

    char nick[IRC_NICK_MAX + 1U];
    char user[IRC_USER_MAX + 1U];
    char realname[IRC_REALNAME_MAX + 1U];
    char away[IRC_AWAY_MAX + 1U];

    /** Authenticated NickServ account name. Empty means not identified. */
    char account_name[IRC_NICK_MAX + 1U];

    /** IRCv3 CAP negotiation and enabled-capability state. */
    int cap_negotiating;
    ClientCapabilitySet capabilities;
    ClientSaslState sasl_state;

    /* The only three normal client host/address identity fields. */
    char real_ip[IRC_IP_MAX + 1U];
    char real_host[IRC_HOST_MAX + 1U];
    char display_host[IRC_HOST_MAX + 1U];

    ClientWebIrc webirc;

    /** MaxMind enrichment for the finalized real_ip; never a public hostname. */
    ClientGeoIP geoip;
    int geoip_complete;

    ClientDnsState dns_state;
    time_t dns_deadline;

    /** DNSBL policy is evaluated asynchronously after real_ip is finalized. */
    ClientDnsblState dnsbl_state;
    time_t dnsbl_deadline;

    time_t signon_time;
    time_t last_activity;

    /** Session-local blocking/presence state. */
    size_t silence_count;
    ClientSilenceEntry *silence_list;
    size_t watch_count;
    ClientWatchEntry *watch_list;

    char quit_reason[IRC_QUIT_REASON_MAX + 1U];
    char inbuf[IRC_INPUT_BUFFER_SIZE];
    size_t inbuf_len;
    size_t channel_count;
    ClientChannelLink *channels;
} Client;

Client *client_create(int fd, uint64_t id, int address_family, const char *ip);
void client_free(void *ptr);
int client_send_raw(Client *client, const char *data, size_t length);
int client_sendf(Client *client, const char *fmt, ...);
int client_send_line(Client *client, const char *line);

#endif
