/**
 * @file common.c
 * @brief Shared helpers used by multiple IRC command implementations.
 */

#include "commands.h"
#include "ban_db.h"
#include "config.h"
#include "geoip.h"
#include "numerics.h"

#include <stdio.h>
#include <sys/socket.h>
#include <time.h>

const char *command_reply_nick(const Client *client) {
    if (client == NULL || client->nick[0] == '\0') return "*";
    return client->nick;
}

static int registration_banned(Server *server, Client *client) {
    BanDb db = {0};
    BanRecord record;
    char host_identity[IRCD_MESSAGE_BUFFER_SIZE];
    char ip_identity[IRCD_MESSAGE_BUFFER_SIZE];
    const char *real_host_identity = NULL;
    int matched = 0;

    if (ban_db_open(&db, server->config.bans_db) != 0) return 0;
    if (ban_db_match(&db, BAN_TYPE_ZLINE, client->real_ip, NULL, &record) == 1) {
        matched = 1;
    } else {
        if (client->real_host[0] != '\0') {
            (void)snprintf(host_identity, sizeof(host_identity), "%s@%s",
                           client->user, client->real_host);
            real_host_identity = host_identity;
        }
        (void)snprintf(ip_identity, sizeof(ip_identity), "%s@%s",
                       client->user, client->real_ip);
        if (ban_db_match(&db, BAN_TYPE_KLINE,
                         real_host_identity != NULL ? real_host_identity : ip_identity,
                         ip_identity, &record) == 1) matched = 1;
    }

    if (matched) {
        client_sendf(client, ERR_YOUREBANNEDCREEP,
                     server->config.server_name, command_reply_nick(client),
                     server->config.admin_email);
        (void)snprintf(client->quit_reason, sizeof(client->quit_reason), "%s",
                       record.reason[0] != '\0' ? record.reason : "Banned");
        (void)shutdown(client->fd, SHUT_RDWR);
    }
    ban_db_close(&db);
    return matched;
}

void command_maybe_register(Server *server, Client *client) {
    if (server == NULL || client == NULL || client->registered ||
        client->nick[0] == '\0' || client->user[0] == '\0' ||
        client->dns_state == CLIENT_DNS_PENDING || client->dns_state == CLIENT_DNS_NONE ||
        (server->config.server_password[0] != '\0' && !client->pass_accepted)) return;

    if (!client->geoip_complete) {
        geoip_lookup(&server->geoip, client->real_ip, &client->geoip);
        client->geoip_complete = 1;
    }

    /*
     * DNSBL policy begins only after direct/WebIRC real_ip is final. Registration
     * remains pending until the asynchronous worker returns or the deadline
     * expires. No configured lists means the stage is immediately clear.
     */
    if (client->dnsbl_state == CLIENT_DNSBL_NONE) {
        if (server->config.dnsbl_count == 0U) {
            client->dnsbl_state = CLIENT_DNSBL_CLEAR;
        } else if (dnsbl_resolver_submit(&server->dnsbl, client->id,
                                          client->real_ip,
                                          server->config.dnsbls,
                                          server->config.dnsbl_count) == 0) {
            client->dnsbl_state = CLIENT_DNSBL_PENDING;
            client->dnsbl_deadline = time(NULL) +
                                     (time_t)server->config.dnsbl_timeout_seconds;
            return;
        } else {
            /* Resolver submission failure fails open but is retained as state. */
            client->dnsbl_state = CLIENT_DNSBL_ERROR;
        }
    }
    if (client->dnsbl_state == CLIENT_DNSBL_PENDING ||
        client->dnsbl_state == CLIENT_DNSBL_LISTED) return;

    if (registration_banned(server, client)) return;

    client->registered = 1;
    client_sendf(client, RPL_WELCOME, server->config.server_name, client->nick,
                 server->config.network_name, client->nick, client->user,
                 client->display_host);
    client_sendf(client, RPL_YOURHOST, server->config.server_name, client->nick,
                 server->config.server_name, IRCD_VERSION);
    client_sendf(client, RPL_CREATED, server->config.server_name, client->nick, IRCD_CREATED);
    client_sendf(client, RPL_AVAILABLE, server->config.server_name, client->nick,
                 server->config.server_name, IRCD_VERSION,
                 IRCD_SUPPORTED_USER_MODES, IRCD_SUPPORTED_CHANNEL_MODES);
    client_sendf(client, RPL_PROTOCOLS, server->config.server_name, client->nick,
                 IRCD_ISUPPORT_BASE);
}

int command_require_registered(Client *client) {
    if (client != NULL && client->registered) return 0;
    if (client != NULL) client_sendf(client, ERR_NOTREGISTERED,
                                      IRCD_DEFAULT_SERVER_NAME, command_reply_nick(client));
    return 1;
}

void command_send_names(Channel *channel, Client *client) {
    char names[IRC_NAMES_BUFFER_SIZE];
    size_t used = 0U;
    ChannelMember *member;
    char marker;
    if (channel == NULL || client == NULL) return;
    marker = channel->name[0] == '&' ? IRC_NAMES_PRIVATE_MARKER : IRC_NAMES_PUBLIC_MARKER;
    names[0] = '\0';
    for (member = channel->members; member != NULL; member = member->next) {
        char prefix = channel_privilege_prefix(member->privileges);
        int written = prefix != '\0'
            ? snprintf(names + used, sizeof(names) - used, "%s%c%s", used ? " " : "", prefix, member->client->nick)
            : snprintf(names + used, sizeof(names) - used, "%s%s", used ? " " : "", member->client->nick);
        if (written < 0 || (size_t)written >= sizeof(names) - used) break;
        used += (size_t)written;
    }
    client_sendf(client, RPL_NAMREPLY, IRCD_DEFAULT_SERVER_NAME, client->nick,
                 marker, channel->name, names);
    client_sendf(client, RPL_ENDOFNAMES, IRCD_DEFAULT_SERVER_NAME, client->nick, channel->name);
}
