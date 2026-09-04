/**
 * @file channel.c
 * @brief Channel ownership, membership, privilege, mask-list, and broadcast helpers.
 *
 * This module owns channel data structures but deliberately does not decide
 * IRC permission policy. MODE/JOIN/KICK/INVITE and services code call these
 * primitives after deciding whether an operation is permitted.
 */

#include "channel.h"
#include "channel_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int channel_name_valid(const char *name) {
    size_t index;
    size_t length;
    if (name == NULL || strchr(IRC_CHANNEL_PREFIXES, name[0]) == NULL) return 0;
    length = strlen(name);
    if (length < 2U || length > IRC_CHANNEL_NAME_MAX) return 0;
    for (index = 1U; index < length; ++index) {
        unsigned char ch = (unsigned char)name[index];
        if (ch <= 0x20U || ch == ',' || ch == ':' || ch == 0x7fU) return 0;
    }
    return 1;
}

Channel *channel_create(const char *name) {
    Channel *channel;

    if (!channel_name_valid(name)) return NULL;
    channel = calloc(1U, sizeof(*channel));
    if (channel == NULL) return NULL;
    snprintf(channel->name, sizeof(channel->name), "%s", name);
    return channel;
}

void channel_mask_clear(ChannelMaskEntry **list) {
    ChannelMaskEntry *entry;
    if (list == NULL) return;
    entry = *list;
    while (entry != NULL) {
        ChannelMaskEntry *next = entry->next;
        free(entry);
        entry = next;
    }
    *list = NULL;
}

void channel_free(void *ptr) {
    Channel *channel = ptr;
    ChannelMember *member;
    if (channel == NULL) return;
    channel_mask_clear(&channel->ban_list);
    channel_mask_clear(&channel->exception_list);
    channel_mask_clear(&channel->invite_exception_list);
    channel_invite_clear(channel);
    channel_join_throttle_clear(channel);
    member = channel->members;
    while (member != NULL) {
        ChannelMember *next = member->next;
        free(member);
        member = next;
    }
    free(channel);
}

ChannelMember *channel_find_member(const Channel *channel, const Client *client) {
    ChannelMember *member;
    if (channel == NULL || client == NULL) return NULL;
    for (member = channel->members; member != NULL; member = member->next)
        if (member->client == client) return member;
    return NULL;
}

int channel_has_client(const Channel *channel, const Client *client) {
    return channel_find_member(channel, client) != NULL;
}

int channel_add_client(Channel *channel, Client *client) {
    ChannelMember *member;
    ClientChannelLink *client_link;
    if (channel == NULL || client == NULL) return -1;
    if (channel_find_member(channel, client) != NULL) return 0;
    member = calloc(1U, sizeof(*member));
    client_link = calloc(1U, sizeof(*client_link));
    if (member == NULL || client_link == NULL) {
        free(member);
        free(client_link);
        return -1;
    }
    member->client = client;
    member->next = channel->members;
    channel->members = member;
    ++channel->member_count;
    client_link->channel = channel;
    client_link->next = client->channels;
    client->channels = client_link;
    ++client->channel_count;
    return 0;
}

void channel_remove_client(Channel *channel, Client *client) {
    ChannelMember **member_link;
    ClientChannelLink **client_link;
    if (channel == NULL || client == NULL) return;
    member_link = &channel->members;
    while (*member_link != NULL) {
        if ((*member_link)->client == client) {
            ChannelMember *dead = *member_link;
            *member_link = dead->next;
            free(dead);
            if (channel->member_count > 0U) --channel->member_count;
            break;
        }
        member_link = &(*member_link)->next;
    }
    client_link = &client->channels;
    while (*client_link != NULL) {
        if ((*client_link)->channel == channel) {
            ClientChannelLink *dead = *client_link;
            *client_link = dead->next;
            free(dead);
            if (client->channel_count > 0U) --client->channel_count;
            break;
        }
        client_link = &(*client_link)->next;
    }
}

int channel_set_privileges(Channel *channel, Client *client,
                           ChannelPrivilegeSet privileges) {
    ChannelMember *member = channel_find_member(channel, client);
    if (member == NULL) return -1;
    member->manual_privileges = privileges;
    member->service_privileges = 0U;
    member->privileges = privileges;
    return 0;
}

int channel_add_privileges(Channel *channel, Client *client,
                           ChannelPrivilegeSet privileges) {
    ChannelMember *member = channel_find_member(channel, client);
    if (member == NULL) return -1;
    member->manual_privileges |= privileges;
    member->privileges = member->manual_privileges | member->service_privileges;
    return 0;
}

int channel_remove_privileges(Channel *channel, Client *client,
                              ChannelPrivilegeSet privileges) {
    ChannelMember *member = channel_find_member(channel, client);
    if (member == NULL) return -1;
    member->manual_privileges &= ~privileges;
    member->service_privileges &= ~privileges;
    member->privileges = member->manual_privileges | member->service_privileges;
    return 0;
}

int channel_set_service_privileges(Channel *channel, Client *client,
                                   ChannelPrivilegeSet privileges) {
    ChannelMember *member = channel_find_member(channel, client);
    if (member == NULL) return -1;
    member->service_privileges = privileges;
    member->privileges = member->manual_privileges | member->service_privileges;
    return 0;
}

void channel_forget_service_privileges(Channel *channel) {
    ChannelMember *member;
    if (channel == NULL) return;
    for (member = channel->members; member != NULL; member = member->next) {
        member->manual_privileges |= member->service_privileges;
        member->service_privileges = 0U;
        member->privileges = member->manual_privileges;
    }
}

int channel_mask_add_authorized(ChannelMaskEntry **list, const char *mask,
                                int protected_authorized) {
    ChannelMaskEntry *entry;
    size_t count = 0U;

    if (list == NULL || mask == NULL || *mask == '\0' ||
        strlen(mask) > IRC_CHANNEL_MASK_MAX) return -1;

    for (entry = *list; entry != NULL; entry = entry->next) {
        ++count;
        if (strcmp(entry->mask, mask) == 0) {
            if (protected_authorized) entry->protected_authorized = 1;
            return 0;
        }
    }
    if (count >= IRC_CHANNEL_MASK_LIST_MAX) return -2;

    entry = calloc(1U, sizeof(*entry));
    if (entry == NULL) return -1;
    snprintf(entry->mask, sizeof(entry->mask), "%s", mask);
    entry->protected_authorized = protected_authorized ? 1 : 0;
    entry->next = *list;
    *list = entry;
    return 0;
}

int channel_mask_add(ChannelMaskEntry **list, const char *mask) {
    return channel_mask_add_authorized(list, mask, 0);
}

int channel_mask_remove(ChannelMaskEntry **list, const char *mask) {
    ChannelMaskEntry **link;
    if (list == NULL || mask == NULL) return 0;
    link = list;
    while (*link != NULL) {
        if (strcmp((*link)->mask, mask) == 0) {
            ChannelMaskEntry *dead = *link;
            *link = dead->next;
            free(dead);
            return 1;
        }
        link = &(*link)->next;
    }
    return 0;
}

void channel_broadcast(Channel *channel, const Client *except, const char *message) {
    ChannelMember *member;
    size_t length;
    char line[IRC_LINE_CONTENT_MAX + 1U];
    if (channel == NULL || message == NULL) return;
    length = strlen(message);
    /* Raw fanout must already be one complete classic IRC frame. Refuse
     * overlength data, missing CRLF framing, or embedded line breaks so a
     * future caller cannot accidentally inject multiple wire messages. */
    if (length < 2U || length > IRC_WIRE_LINE_MAX ||
        message[length - 2U] != '\r' || message[length - 1U] != '\n' ||
        memchr(message, '\r', length - 2U) != NULL ||
        memchr(message, '\n', length - 2U) != NULL)
        return;
    memcpy(line, message, length - 2U);
    line[length - 2U] = '\0';
    for (member = channel->members; member != NULL; member = member->next) {
        if (member->client == except) continue;
        (void)client_send_line(member->client, line);
    }
}
