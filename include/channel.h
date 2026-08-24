#ifndef IRCD_CHANNEL_H
#define IRCD_CHANNEL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "client.h"
#include "config.h"
#include "modes.h"

/** One mask entry used by +b, +e, or +I. */
typedef struct ChannelMaskEntry {
    char mask[IRC_CHANNEL_MASK_MAX + 1U];
    /** Non-zero only for a +b set by an OWNER or PROTECTED member. */
    int protected_authorized;
    struct ChannelMaskEntry *next;
} ChannelMaskEntry;

/** One explicit one-use invitation, keyed by stable connection ID. */
typedef struct ChannelInvite {
    uint64_t client_id;
    struct ChannelInvite *next;
} ChannelInvite;

/** One client's membership and privileges in one channel. */
typedef struct ChannelMember {
    Client *client;
    ChannelPrivilegeSet privileges;
    struct ChannelMember *next;
} ChannelMember;

/** Complete state for one IRC channel. */
typedef struct Channel {
    char name[IRC_CHANNEL_NAME_MAX + 1U];
    ChannelModeSet modes;

    /**
     * Current topic state. topic_setter is the full nick!user@host identity
     * that last changed it; topic_time is zero when no topic has been set.
     */
    char topic[IRC_CHANNEL_TOPIC_MAX + 1U];
    char topic_setter[IRC_CHANNEL_TOPIC_SETTER_MAX + 1U];
    time_t topic_time;

    char key[IRC_CHANNEL_KEY_MAX + 1U];
    size_t user_limit;
    unsigned int join_throttle_count;
    unsigned int join_throttle_seconds;
    /** Channel-wide +j join window. */
    unsigned int join_throttle_window_count;
    time_t join_throttle_window_start;
    char limit_redirect[IRC_CHANNEL_NAME_MAX + 1U];
    char ban_redirect[IRC_CHANNEL_NAME_MAX + 1U];

    ChannelMaskEntry *ban_list;
    ChannelMaskEntry *exception_list;
    ChannelMaskEntry *invite_exception_list;
    ChannelInvite *invites;

    ChannelMember *members;
    size_t member_count;
} Channel;

Channel *channel_create(const char *name);
void channel_free(void *ptr);
ChannelMember *channel_find_member(const Channel *channel, const Client *client);
int channel_has_client(const Channel *channel, const Client *client);
int channel_add_client(Channel *channel, Client *client);
void channel_remove_client(Channel *channel, Client *client);
int channel_set_privileges(Channel *channel, Client *client,
                           ChannelPrivilegeSet privileges);
int channel_add_privileges(Channel *channel, Client *client,
                           ChannelPrivilegeSet privileges);
int channel_remove_privileges(Channel *channel, Client *client,
                              ChannelPrivilegeSet privileges);
void channel_broadcast(Channel *channel, const Client *except, const char *message);
int channel_mask_add(ChannelMaskEntry **list, const char *mask);
int channel_mask_add_authorized(ChannelMaskEntry **list, const char *mask,
                                int protected_authorized);
int channel_mask_remove(ChannelMaskEntry **list, const char *mask);
void channel_mask_clear(ChannelMaskEntry **list);

#endif /* IRCD_CHANNEL_H */
