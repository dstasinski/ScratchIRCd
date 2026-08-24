/**
 * @file channel_policy.c
 * @brief IRC mask matching, access-list evaluation, invites, and join throttling.
 */

#include "channel_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned char rfc1459_fold(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return (unsigned char)(ch + ('a' - 'A'));
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

    if (pattern == NULL || text == NULL) return 0;

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

    while (*pattern == '*') ++pattern;
    return *pattern == '\0';
}

static int mask_entry_matches_client(const ChannelMaskEntry *entry,
                                     const Client *client) {
    char identity[IRCD_MESSAGE_BUFFER_SIZE];
    if (entry == NULL || client == NULL) return 0;
    (void)snprintf(identity, sizeof(identity), "%s!%s@%s",
                   client->nick, client->user, client->display_host);
    return irc_mask_match(entry->mask, identity);
}

/**
 * Match a channel ban/exception/invex against the client's public identity.
 * Channel masks intentionally use display_host only.
 */
int channel_mask_matches_client(const ChannelMaskEntry *list,
                                const Client *client) {
    const ChannelMaskEntry *entry;
    if (client == NULL) return 0;
    for (entry = list; entry != NULL; entry = entry->next)
        if (mask_entry_matches_client(entry, client)) return 1;
    return 0;
}

static int authorized_ban_matches_client(const ChannelMaskEntry *list,
                                         const Client *client) {
    const ChannelMaskEntry *entry;
    if (client == NULL) return 0;
    for (entry = list; entry != NULL; entry = entry->next) {
        if (entry->protected_authorized &&
            mask_entry_matches_client(entry, client)) return 1;
    }
    return 0;
}

int channel_client_is_banned(const Channel *channel, const Client *client) {
    if (channel == NULL || client == NULL) return 0;
    return channel_mask_matches_client(channel->ban_list, client) &&
           !channel_mask_matches_client(channel->exception_list, client);
}

int channel_client_is_banned_protected(const Channel *channel,
                                       const Client *client) {
    if (channel == NULL || client == NULL) return 0;
    return authorized_ban_matches_client(channel->ban_list, client) &&
           !channel_mask_matches_client(channel->exception_list, client);
}

int channel_client_is_invex(const Channel *channel, const Client *client) {
    return channel != NULL && client != NULL &&
           channel_mask_matches_client(channel->invite_exception_list, client);
}

int channel_invite_add(Channel *channel, uint64_t client_id) {
    ChannelInvite *invite;

    if (channel == NULL || client_id == 0U) return -1;
    if (channel_invite_has(channel, client_id)) return 0;

    invite = calloc(1U, sizeof(*invite));
    if (invite == NULL) return -1;
    invite->client_id = client_id;
    invite->next = channel->invites;
    channel->invites = invite;
    return 0;
}

int channel_invite_has(const Channel *channel, uint64_t client_id) {
    ChannelInvite *invite;

    if (channel == NULL || client_id == 0U) return 0;
    for (invite = channel->invites; invite != NULL; invite = invite->next) {
        if (invite->client_id == client_id) return 1;
    }
    return 0;
}

int channel_invite_consume(Channel *channel, uint64_t client_id) {
    ChannelInvite **link;

    if (channel == NULL || client_id == 0U) return 0;
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
    if (channel == NULL) return;
    invite = channel->invites;
    while (invite != NULL) {
        ChannelInvite *next = invite->next;
        free(invite);
        invite = next;
    }
    channel->invites = NULL;
}

int channel_join_throttle_allows(Channel *channel, uint64_t client_id) {
    time_t now;
    (void)client_id;
    if (channel == NULL ||
        channel->join_throttle_count == 0U ||
        channel->join_throttle_seconds == 0U) return 1;

    now = time(NULL);
    if (channel->join_throttle_window_count == 0U ||
        now < channel->join_throttle_window_start ||
        (unsigned long)(now - channel->join_throttle_window_start) >=
            (unsigned long)channel->join_throttle_seconds) return 1;

    return channel->join_throttle_window_count < channel->join_throttle_count;
}

void channel_join_throttle_record(Channel *channel, uint64_t client_id) {
    time_t now;
    (void)client_id;
    if (channel == NULL ||
        channel->join_throttle_count == 0U ||
        channel->join_throttle_seconds == 0U) return;

    now = time(NULL);
    if (channel->join_throttle_window_count == 0U ||
        now < channel->join_throttle_window_start ||
        (unsigned long)(now - channel->join_throttle_window_start) >=
            (unsigned long)channel->join_throttle_seconds) {
        channel->join_throttle_window_start = now;
        channel->join_throttle_window_count = 1U;
        return;
    }
    ++channel->join_throttle_window_count;
}

void channel_join_throttle_clear(Channel *channel) {
    if (channel == NULL) return;
    channel->join_throttle_window_count = 0U;
    channel->join_throttle_window_start = 0;
}
