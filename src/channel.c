#include "channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

Channel *channel_create(const char *name) {
    Channel *channel;

    if (name == NULL) {
        return NULL;
    }

    channel = calloc(1U, sizeof(*channel));
    if (channel == NULL) {
        return NULL;
    }

    snprintf(channel->name, sizeof(channel->name), "%s", name);
    return channel;
}

void channel_free(void *ptr) {
    Channel *channel = ptr;
    ChannelMember *member;

    if (channel == NULL) {
        return;
    }

    member = channel->members;
    while (member != NULL) {
        ChannelMember *next = member->next;
        free(member);
        member = next;
    }
    free(channel);
}

int channel_has_client(const Channel *channel, const Client *client) {
    ChannelMember *member;

    if (channel == NULL || client == NULL) {
        return 0;
    }

    for (member = channel->members; member != NULL; member = member->next) {
        if (member->client == client) {
            return 1;
        }
    }
    return 0;
}

int channel_add_client(Channel *channel, Client *client) {
    ChannelMember *member;
    ClientChannelLink *client_link;

    if (channel == NULL || client == NULL) {
        return -1;
    }
    if (channel_has_client(channel, client)) {
        return 0;
    }

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

    if (channel == NULL || client == NULL) {
        return;
    }

    member_link = &channel->members;
    while (*member_link != NULL) {
        if ((*member_link)->client == client) {
            ChannelMember *dead = *member_link;
            *member_link = dead->next;
            free(dead);
            if (channel->member_count > 0U) {
                --channel->member_count;
            }
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
            if (client->channel_count > 0U) {
                --client->channel_count;
            }
            break;
        }
        client_link = &(*client_link)->next;
    }
}

void channel_broadcast(Channel *channel, const Client *except, const char *message) {
    ChannelMember *member;
    size_t length;

    if (channel == NULL || message == NULL) {
        return;
    }

    length = strlen(message);
    for (member = channel->members; member != NULL; member = member->next) {
        if (member->client == except) {
            continue;
        }
        (void)send(member->client->fd, message, length, MSG_NOSIGNAL);
    }
}
