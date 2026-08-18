#ifndef IRCD_CLIENT_H
#define IRCD_CLIENT_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

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

/** Complete state for one connected IRC client. */
typedef struct Client {
    uint64_t id;
    int fd;
    int address_family;
    int registered;
    int is_webirc;
    int pass_accepted;                   /**< PASS satisfied when required. */
    ClientModeSet modes;

    /** Authorization granted by OPER or, later, an identified account. */
    OperPermissionSet oper_permissions;
    char oper_name[IRCD_OPER_NAME_MAX + 1U]; /**< Matched operator/account name. */

    char nick[IRC_NICK_MAX + 1U];
    char user[IRC_USER_MAX + 1U];
    char realname[IRC_REALNAME_MAX + 1U];
    char away[IRC_AWAY_MAX + 1U];

    char ip[IRC_IP_MAX + 1U];
    char host[IRC_HOST_MAX + 1U];
    char reverse_host[IRC_HOST_MAX + 1U];
    char forward_host[IRC_HOST_MAX + 1U];
    char socket_ip[IRC_IP_MAX + 1U];
    char socket_host[IRC_HOST_MAX + 1U];

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
int client_sendf(Client *client, const char *fmt, ...);
int client_send_line(Client *client, const char *line);

#endif /* IRCD_CLIENT_H */
