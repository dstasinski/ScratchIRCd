/**
 * @file ircv3.c
 * @brief Shared IRCv3 capability-dependent protocol behavior.
 */

#include "ircv3.h"
#include "channel.h"

#include <stdio.h>

/** Return true when recipient already appeared in a channel visited before stop. */
static int seen_in_earlier_channel(const Client *source,
                                   const ClientChannelLink *stop,
                                   const Client *recipient) {
    const ClientChannelLink *link;
    for (link = source->channels; link != NULL && link != stop; link = link->next)
        if (channel_has_client(link->channel, recipient)) return 1;
    return 0;
}

void ircv3_account_notify(Client *client) {
    ClientChannelLink *link;
    char message[IRCD_MESSAGE_BUFFER_SIZE];
    const char *account;

    if (client == NULL || !client->registered) return;
    account = client->account_name[0] != '\0' ? client->account_name : "*";
    (void)snprintf(message, sizeof(message), ":%s!%s@%s ACCOUNT %s",
                   client->nick, client->user, client->display_host, account);

    for (link = client->channels; link != NULL; link = link->next) {
        ChannelMember *member;
        for (member = link->channel->members; member != NULL; member = member->next) {
            Client *recipient = member->client;
            if (recipient == client ||
                (recipient->capabilities & CLIENT_CAP_ACCOUNT_NOTIFY) == 0U ||
                seen_in_earlier_channel(client, link, recipient)) continue;
            client_send_line(recipient, message);
        }
    }
}
