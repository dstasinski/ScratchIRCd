/**
 * @file visibility.c
 * @brief Shared visibility decisions for IRC information commands.
 */

#include "visibility.h"

#include "modes.h"

int visibility_is_oper(const Client *requester) {
    return requester != NULL &&
           client_mode_has(requester->modes,
                           CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN);
}

int visibility_share_channel(const Client *requester, const Client *subject) {
    const ClientChannelLink *link;

    if (requester == NULL || subject == NULL) {
        return 0;
    }
    if (requester == subject) {
        return 1;
    }

    for (link = requester->channels; link != NULL; link = link->next) {
        if (channel_has_client(link->channel, subject)) {
            return 1;
        }
    }
    return 0;
}

int visibility_list_channel(const Client *requester, const Channel *channel) {
    if (channel == NULL) {
        return 0;
    }

    /* '&' channels are explicitly local/private and omitted from normal LIST. */
    if (channel->name[0] == '&') {
        return 0;
    }

    if (visibility_is_oper(requester) ||
        channel_has_client(channel, requester)) {
        return 1;
    }

    if (channel_mode_has(channel->modes,
                         CHANNEL_MODE_PRIVATE | CHANNEL_MODE_SECRET)) {
        return 0;
    }
    return 1;
}

int visibility_names_channel(const Client *requester, const Channel *channel) {
    if (channel == NULL) {
        return 0;
    }
    if (visibility_is_oper(requester) ||
        channel_has_client(channel, requester)) {
        return 1;
    }
    return !channel_mode_has(channel->modes, CHANNEL_MODE_SECRET);
}

int visibility_who_user(const Client *requester, const Client *subject) {
    if (requester == NULL || subject == NULL || !subject->registered) {
        return 0;
    }
    if (requester == subject || visibility_is_oper(requester)) {
        return 1;
    }
    if (!client_mode_has(subject->modes, CLIENT_MODE_INVISIBLE)) {
        return 1;
    }
    return visibility_share_channel(requester, subject);
}

int visibility_whois_channel(const Client *requester, const Client *subject,
                             const Channel *channel) {
    if (requester == NULL || subject == NULL || channel == NULL) {
        return 0;
    }
    if (requester == subject || visibility_is_oper(requester)) {
        return 1;
    }
    if (client_mode_has(subject->modes, CLIENT_MODE_PRIVATE)) {
        return 0;
    }
    if (channel_has_client(channel, requester)) {
        return 1;
    }
    return !channel_mode_has(channel->modes,
                             CHANNEL_MODE_PRIVATE | CHANNEL_MODE_SECRET) &&
           channel->name[0] != '&';
}
