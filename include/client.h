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

typedef struct ClientChannelLink { struct Channel *channel; struct ClientChannelLink *next; } ClientChannelLink;
typedef struct ClientSilenceEntry { char mask[IRC_CHANNEL_MASK_MAX + 1U]; struct ClientSilenceEntry *next; } ClientSilenceEntry;
typedef struct ClientWatchEntry { char nick[IRC_NICK_MAX + 1U]; struct ClientWatchEntry *next; } ClientWatchEntry;
typedef enum ClientDnsState { CLIENT_DNS_NONE = 0, CLIENT_DNS_PENDING, CLIENT_DNS_VERIFIED, CLIENT_DNS_FAILED, CLIENT_DNS_TIMEOUT } ClientDnsState;
typedef enum ClientDnsblState { CLIENT_DNSBL_NONE = 0, CLIENT_DNSBL_PENDING, CLIENT_DNSBL_CLEAR, CLIENT_DNSBL_LISTED, CLIENT_DNSBL_TIMEOUT, CLIENT_DNSBL_ERROR } ClientDnsblState;
typedef enum ClientTlsState { CLIENT_TLS_NONE = 0, CLIENT_TLS_HANDSHAKE, CLIENT_TLS_ESTABLISHED } ClientTlsState;
typedef enum ClientSaslState { CLIENT_SASL_NONE = 0, CLIENT_SASL_PLAIN_WAIT_DATA, CLIENT_SASL_COMPLETE, CLIENT_SASL_FAILED } ClientSaslState;

typedef uint64_t ClientCapabilitySet;
#define CLIENT_CAP_SASL             (UINT64_C(1) << 0)
#define CLIENT_CAP_ACCOUNT_NOTIFY   (UINT64_C(1) << 1)
#define CLIENT_CAP_BATCH            (UINT64_C(1) << 2)
#define CLIENT_CAP_SERVER_TIME      (UINT64_C(1) << 3)
#define CLIENT_CAP_CHATHISTORY      (UINT64_C(1) << 4)
#define CLIENT_CAP_AWAY_NOTIFY      (UINT64_C(1) << 5)
#define CLIENT_CAP_EXTENDED_JOIN    (UINT64_C(1) << 6)
#define CLIENT_CAP_LABELED_RESPONSE (UINT64_C(1) << 7)
#define CLIENT_CAP_MESSAGE_TAGS     (UINT64_C(1) << 8)

typedef struct ClientWebIrc { int active; char gateway_ip[IRC_IP_MAX + 1U]; char gateway_name[IRCD_WEBIRC_GATEWAY_NAME_MAX + 1U]; char supplied_host[IRC_HOST_MAX + 1U]; } ClientWebIrc;

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
    int input_retry_pending;
    int input_want_write;

    /* Bounded nonblocking output queue. */
    char *outbuf;
    size_t outbuf_start;
    size_t outbuf_len;
    size_t outbuf_capacity;
    size_t outbuf_limit;
    int output_retry_pending;
    int output_want_read;
    size_t output_retry_length;
    int output_overflowed;

    /* Token budget used only for expensive/read-amplifying commands. */
    unsigned int command_budget_tokens;
    time_t command_budget_updated;
    time_t command_throttle_notice_time;

    /* Independent budget for aggregate inbound command/event-loop pressure. */
    unsigned int flood_budget_tokens;
    time_t flood_budget_updated;
    time_t flood_violation_window;
    unsigned int flood_violation_count;

    OperPermissionSet oper_permissions;
    char oper_name[IRCD_OPER_NAME_MAX + 1U];
    SnoticeMask snotice_mask;

    char nick[IRC_NICK_MAX + 1U];
    char user[IRC_USER_MAX + 1U];
    char realname[IRC_REALNAME_MAX + 1U];
    char away[IRC_AWAY_MAX + 1U];
    char account_name[IRC_NICK_MAX + 1U];

    int cap_negotiating;
    unsigned int cap_version;
    ClientCapabilitySet capabilities;
    ClientSaslState sasl_state;
    char *sasl_buffer;
    size_t sasl_buffer_len;
    size_t sasl_buffer_capacity;

    /* Per-command IRCv3 metadata. Client-only tags are retained only while
     * the current command is dispatched. Labeled replies are grouped in a
     * short-lived labeled-response batch by client_send_line(). */
    char ircv3_client_tags[IRCV3_CLIENT_TAG_DATA_MAX + 1U];
    int labeled_response_active;
    int labeled_response_started;
    unsigned int labeled_response_suppressed;
    char labeled_response_label[IRCV3_LABEL_ENCODED_MAX + 1U];
    char labeled_response_batch[IRCD_HISTORY_BATCH_ID_MAX + 1U];
    char labeled_response_server[IRC_SERVER_NAME_MAX + 1U];

    char real_ip[IRC_IP_MAX + 1U];
    char real_host[IRC_HOST_MAX + 1U];
    char display_host[IRC_HOST_MAX + 1U];
    /** True only while +t/display_host came from the authenticated account. */
    int account_vhost_active;
    ClientWebIrc webirc;

    int nospoof_started;
    int nospoof_verified;
    time_t nospoof_deadline;
    char nospoof_cookie[IRCD_NOSPOOF_COOKIE_HEX_LEN + 1U];
    int version_requested;
    int version_received;
    char client_version[IRCD_CLIENT_VERSION_MAX + 1U];
    int website_requested;
    int website_received;
    char client_website[IRCD_CLIENT_WEBSITE_MAX + 1U];

    ClientGeoIP geoip;
    int geoip_complete;
    ClientDnsState dns_state;
    time_t dns_deadline;
    ClientDnsblState dnsbl_state;
    time_t dnsbl_deadline;
    time_t signon_time;
    /** User-visible idle clock. Only delivered user/channel PRIVMSG resets it. */
    time_t last_activity;
    /** Transport liveness clock, independent from WHOIS idle accounting. */
    time_t last_liveness_activity;
    int ping_pending;
    time_t ping_deadline;
    char ping_token[IRCD_PING_TOKEN_MAX + 1U];

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

typedef void (*ClientFreeHook)(Client *client);

Client *client_create(int fd, uint64_t id, int address_family, const char *ip);
void client_set_free_hook(ClientFreeHook hook);
void client_set_output_limit(Client *client, size_t limit);
int client_output_pending(const Client *client);
int client_flush_output(Client *client);
void client_free(void *ptr);
int client_send_raw(Client *client, const char *data, size_t length);
int client_sendf(Client *client, const char *fmt, ...);
int client_send_line(Client *client, const char *line);
void client_labeled_response_begin(Client *client, const char *server_name,
                                   const char *label);
void client_labeled_response_end(Client *client);
void client_labeled_response_suppress(Client *client, int suppress);

#endif
