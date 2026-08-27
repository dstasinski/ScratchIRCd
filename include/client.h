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

typedef struct Channel Channel;

typedef enum ClientDnsState { CLIENT_DNS_NONE=0, CLIENT_DNS_PENDING, CLIENT_DNS_VERIFIED, CLIENT_DNS_FAILED, CLIENT_DNS_TIMEOUT } ClientDnsState;
typedef enum ClientDnsblState { CLIENT_DNSBL_NONE=0, CLIENT_DNSBL_PENDING, CLIENT_DNSBL_CLEAR, CLIENT_DNSBL_LISTED, CLIENT_DNSBL_TIMEOUT, CLIENT_DNSBL_ERROR } ClientDnsblState;
typedef enum ClientTlsState { CLIENT_TLS_NONE=0, CLIENT_TLS_HANDSHAKE, CLIENT_TLS_ESTABLISHED } ClientTlsState;
typedef enum ClientSaslState { CLIENT_SASL_NONE=0, CLIENT_SASL_WAITING, CLIENT_SASL_PLAIN_WAIT_DATA, CLIENT_SASL_FAILED, CLIENT_SASL_COMPLETE } ClientSaslState;

typedef struct ClientChannelLink { Channel *channel; struct ClientChannelLink *next; } ClientChannelLink;
typedef struct ClientSilenceEntry { char mask[IRC_CHANNEL_MASK_MAX+1U]; struct ClientSilenceEntry *next; } ClientSilenceEntry;
typedef struct ClientWatchEntry { char nick[IRC_NICK_MAX+1U]; struct ClientWatchEntry *next; } ClientWatchEntry;
typedef struct ClientWebIrcInfo { int active; char gateway_ip[IRC_IP_MAX+1U]; char gateway_name[IRC_HOST_MAX+1U]; char supplied_host[IRC_HOST_MAX+1U]; } ClientWebIrcInfo;
typedef void (*ClientFreeHook)(struct Client *client);

typedef struct Client {
    uint64_t id;
    int fd;
    int address_family;
    SSL *ssl;
    ClientTlsState tls_state;
    int tls_want_write;
    int input_want_write;
    char real_ip[IRC_IP_MAX+1U];
    char real_host[IRC_HOST_MAX+1U];
    char display_host[IRC_HOST_MAX+1U];
    ClientDnsState dns_state;
    time_t dns_deadline;
    ClientDnsblState dnsbl_state;
    time_t dnsbl_deadline;
    GeoIpInfo geoip;
    int geoip_complete;
    char nick[IRC_NICK_MAX+1U];
    char user[IRC_USER_MAX+1U];
    char realname[IRC_REALNAME_MAX+1U];
    char account_name[IRC_NICK_MAX+1U];
    char away[IRC_AWAY_MAX+1U];
    char client_version[IRCD_CLIENT_VERSION_MAX+1U];
    char client_website[IRCD_CLIENT_WEBSITE_MAX+1U];
    int client_version_requested;
    int client_version_received;
    int client_website_requested;
    int client_website_received;
    ClientWebIrcInfo webirc;
    ClientModeSet modes;
    OperPermissionSet oper_permissions;
    char oper_name[IRCD_OPER_NAME_MAX+1U];
    int pass_accepted;
    int registered;
    int cap_negotiating;
    unsigned int caps;
    ClientSaslState sasl_state;
    char sasl_buffer[IRCD_SASL_BUFFER_MAX+1U];
    size_t sasl_buffer_len;
    int nospoof_started;
    int nospoof_verified;
    char nospoof_cookie[IRCD_NOSPOOF_COOKIE_HEX_LEN+1U];
    time_t nospoof_deadline;
    time_t signon_time;
    time_t last_activity;
    char inbuf[IRCD_INPUT_BUFFER_SIZE];
    size_t inbuf_len;
    char *outbuf;
    size_t outbuf_start;
    size_t outbuf_len;
    size_t outbuf_capacity;
    size_t outbuf_limit;
    int output_overflowed;
    int output_want_read;
    char quit_reason[IRC_QUIT_REASON_MAX+1U];
    ClientChannelLink *channels;
    size_t channel_count;
    ClientSilenceEntry *silence_list;
    size_t silence_count;
    ClientWatchEntry *watch_list;
    size_t watch_count;
    time_t flood_last_refill;
    unsigned int flood_tokens;
    time_t flood_last_notice;
    time_t command_budget_updated;
    unsigned int command_budget_tokens;
    time_t command_budget_notice_time;
} Client;

Client *client_create(int fd, uint64_t id, int address_family, const char *ip);
void client_free(void *ptr);
void client_set_free_hook(ClientFreeHook hook);
void client_set_output_limit(Client *client, size_t limit);
int client_output_pending(const Client *client);
int client_flush_output(Client *client);
int client_send_raw(Client *client, const char *data, size_t length);
int client_send_line(Client *client, const char *line);
int client_sendf(Client *client, const char *fmt, ...);

#endif
