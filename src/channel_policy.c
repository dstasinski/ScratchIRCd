/**
 * @file channel_policy.c
 * @brief IRC mask matching, ban/exception evaluation, and transient invites.
 */

#include "channel_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Fold one byte using RFC1459 casemapping. */
static unsigned char rfc1459_fold(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (unsigned char)(ch + ('a' - 'A'));
    }
    switch (ch) {
        case '[': return '{';
        case ']': return '}';
        case '\\': return '|';
        case '^': return '~';
        default: return ch;
    }
}

int irc_mask_match(const char *pattern, const char *text) {
    const char *star = NULL;
    const char *retry = NULL;

    if (pattern == NULL || text == NULL) {
        return 0;
    }

    while (*text != '\0') {
        if (*pattern == '*') {
            star = ++pattern;
            retry = text;
            continue;
        }

        if (*pattern == '?' ||
            rfc1459_fold((unsigned char)*pattern) ==
                rfc1459_fold((unsigned char)*text)) {
            ++pattern;
            ++text;
            continue;
        }

        if (star != NULL) {
            pattern = star;
            text = ++retry;
            continue;
        }

        return 0;
    }

    while (*pattern == '*') {
        ++pattern;
    }
    return *pattern == '\0';
}

int channel_mask_matches_client(const ChannelMaskEntry *list,
                                const Client *client) {
    char hostmask[IRC_CHANNEL_MASK_MAX + 1U];
    char ipmask[IRC_CHANNEL_MASK_MAX + 1U];
    const ChannelMaskEntry *entry;

    if (client == NULL) {
        return 0;
    }

    (void)snprintf(hostmask, sizeof(hostmask), "%s!%s@%s",
                   client->nick, client->user, client->host);
    (void)snprintf(ipmask, sizeof(ipmask), "%s!%s@%s",
                   client->nick, client->user, client->ip);

    for (entry = list; entry != NULL; entry = entry->next) {
        if (irc_mask_match(entry->mask, hostmask) ||
            irc_mask_match(entry->mask, ipmask)) {
            return 1;
        }
    }
    return 0;
}

int channel_client_is_banned(const Channel *channel, const Client *client) {
    if (channel == NULL || client == NULL) {
        return 0;
    }

    return channel_mask_matches_client(channel->ban_list, client) &&
           !channel_mask_matches_client(channel->exception_list, client);
}

int channel_client_is_invex(const Channel *channel, const Client *client) {
    return channel != NULL && client != NULL &&
           channel_mask_matches_client(channel->invite_exception_list, client);
}

int channel_invite_add(Channel *channel, uint64_t client_id) {
    ChannelInvite *invite;

    if (channel == NULL || client_id == 0U) {
        return -1;
    }
    if (channel_invite_has(channel, client_id)) {
        return 0;
    }

    invite = calloc(1U, sizeof(*invite));
    if (invite == NULL) {
        return -1;
    }

    invite->client_id = client_id;
    invite->next = channel->invites;
    channel->invites = invite;
    return 0;
}

int channel_invite_has(const Channel *channel, uint64_t client_id) {
    ChannelInvite *invite;

    if (channel == NULL || client_id == 0U) {
        return 0;
    }

    for (invite = channel->invites; invite != NULL; invite = invite->next) {
        if (invite->client_id == client_id) {
            return 1;
        }
    }
    return 0;
}

int channel_invite_consume(Channel *channel, uint64_t client_id) {
    ChannelInvite **link;

    if (channel == NULL || client_id == 0U) {
        return 0;
    }

    link = &channel->invites;
    while (*link != NULL) {
        if ((*link)->client_id == client_id) {
            ChannelInvite *dead = *link;
            *link = dead->next;
            free(dead);
            return 1;
        }
        link = &(*link)->next;
    }
    return 0;
}

void channel_invite_clear(Channel *channel) {
    ChannelInvite *invite;

    if (channel == NULL) {
        return;
    }

    invite = channel->invites;
    while (invite != NULL) {
        ChannelInvite *next = invite->next;
        free(invite);
        invite = next;
    }
    channel->invites = NULL;
}
