/**
 * @file common.c
 * @brief Shared helpers used by multiple IRC command implementations.
 *
 * This file intentionally contains no IRC command handler.  It centralizes
 * small pieces of protocol behavior that would otherwise be duplicated across
 * command files: the pre-registration nickname placeholder, registration
 * completion, the registration guard, and NAMES numeric generation.
 *
 * Numeric replies are formatted exclusively through include/numerics.h.
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

void command_maybe_register(Client *client) {
    if (client == NULL || client->registered || client->nick[0] == '\0' ||
        client->user[0] == '\0') {
        return;
    }

    client->registered = 1;

    client_sendf(client, RPL_WELCOME,
                 IRCD_SERVER_NAME, client->nick, IRCD_NETWORK_NAME,
                 client->nick, client->user, client->host);
    client_sendf(client, RPL_YOURHOST,
                 IRCD_SERVER_NAME, client->nick,
                 IRCD_SERVER_NAME, IRCD_VERSION);
    client_sendf(client, RPL_CREATED,
                 IRCD_SERVER_NAME, client->nick, IRCD_CREATED);
    client_sendf(client, RPL_AVAILABLE,
                 IRCD_SERVER_NAME, client->nick, IRCD_SERVER_NAME,
                 IRCD_VERSION, IRCD_SUPPORTED_USER_MODES,
                 IRCD_SUPPORTED_CHANNEL_MODES);
    client_sendf(client, RPL_PROTOCOLS,
                 IRCD_SERVER_NAME, client->nick, IRCD_ISUPPORT);
}

int command_require_registered(Client *client) {
    if (client != NULL && client->registered) {
        return 0;
    }

    if (client != NULL) {
        client_sendf(client, ERR_NOTREGISTERED,
                     IRCD_SERVER_NAME, command_reply_nick(client));
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
                 IRCD_SERVER_NAME, client->nick, IRC_NAMES_PUBLIC_MARKER,
                 channel->name, names);
    client_sendf(client, RPL_ENDOFNAMES,
                 IRCD_SERVER_NAME, client->nick, channel->name);
}
