/**
 * @file common.c
 * @brief Shared helpers used by multiple IRC command implementations.
 *
 * Registration deliberately waits while asynchronous DNS is pending.  A
 * failed or timed-out lookup does not reject the client; the numeric address
 * remains the effective hostname and registration proceeds normally.
 */

#include "commands.h"
#include "config.h"
#include "numerics.h"

#include <stdio.h>

const char *command_reply_nick(const Client *client) {
    if (client == NULL || client->nick[0] == '\0') {
        return "*";
    }
    return client->nick;
}

void command_maybe_register(Server *server, Client *client) {
    if (server == NULL || client == NULL || client->registered ||
        client->nick[0] == '\0' || client->user[0] == '\0' ||
        client->dns_state == CLIENT_DNS_PENDING ||
        client->dns_state == CLIENT_DNS_NONE) {
        return;
    }

    client->registered = 1;

    client_sendf(client, RPL_WELCOME,
                 server->config.server_name, client->nick,
                 server->config.network_name,
                 client->nick, client->user, client->host);
    client_sendf(client, RPL_YOURHOST,
                 server->config.server_name, client->nick,
                 server->config.server_name, IRCD_VERSION);
    client_sendf(client, RPL_CREATED,
                 server->config.server_name, client->nick, IRCD_CREATED);
    client_sendf(client, RPL_AVAILABLE,
                 server->config.server_name, client->nick,
                 server->config.server_name, IRCD_VERSION,
                 IRCD_SUPPORTED_USER_MODES, IRCD_SUPPORTED_CHANNEL_MODES);
    client_sendf(client, RPL_PROTOCOLS,
                 server->config.server_name, client->nick, IRCD_ISUPPORT_BASE);
}

int command_require_registered(Client *client) {
    if (client != NULL && client->registered) {
        return 0;
    }

    if (client != NULL) {
        client_sendf(client, ERR_NOTREGISTERED,
                     IRCD_DEFAULT_SERVER_NAME, command_reply_nick(client));
    }
    return 1;
}

void command_send_names(Channel *channel, Client *client) {
    char names[IRC_NAMES_BUFFER_SIZE];
    size_t used = 0U;
    ChannelMember *member;

    if (channel == NULL || client == NULL) {
        return;
    }

    names[0] = '\0';
    for (member = channel->members; member != NULL; member = member->next) {
        int written = snprintf(names + used, sizeof(names) - used,
                               "%s%s", used != 0U ? " " : "",
                               member->client->nick);
        if (written < 0 || (size_t)written >= sizeof(names) - used) {
            break;
        }
        used += (size_t)written;
    }

    client_sendf(client, RPL_NAMREPLY,
                 IRCD_DEFAULT_SERVER_NAME, client->nick,
                 IRC_NAMES_PUBLIC_MARKER, channel->name, names);
    client_sendf(client, RPL_ENDOFNAMES,
                 IRCD_DEFAULT_SERVER_NAME, client->nick, channel->name);
}
