#ifndef IRCD_CHANNEL_H
#define IRCD_CHANNEL_H

#include <stddef.h>
#include "client.h"
#include "config.h"

/** One member in a channel's singly linked membership list. */
typedef struct ChannelMember {
    Client *client;                    /**< Client represented by this member. */
    struct ChannelMember *next;        /**< Next channel member. */
} ChannelMember;

/**
 * State for one IRC channel.
 *
 * Channel objects are owned by Server.channels_by_name and are destroyed as
 * soon as their final member leaves.
 */
typedef struct Channel {
    char name[IRC_CHANNEL_NAME_MAX + 1U]; /**< Canonical spelling from creation. */
    ChannelMember *members;               /**< Head of member list. */
    size_t member_count;                  /**< Current number of members. */
} Channel;

/** Allocate an empty channel using name. */
Channel *channel_create(const char *name);

/** Free a Channel and any remaining membership nodes. */
void channel_free(void *ptr);

/** Return non-zero when client is a member of channel. */
int channel_has_client(const Channel *channel, const Client *client);

/** Add a bidirectional client/channel membership link. */
int channel_add_client(Channel *channel, Client *client);

/** Remove the bidirectional membership link, if present. */
void channel_remove_client(Channel *channel, Client *client);

/** Broadcast a complete IRC message to all members except optional except. */
void channel_broadcast(Channel *channel, const Client *except, const char *message);

#endif /* IRCD_CHANNEL_H */
