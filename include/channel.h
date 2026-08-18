#ifndef IRCD_CHANNEL_H
#define IRCD_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#include "client.h"
#include "config.h"
#include "modes.h"

/**
 * One mask entry used by channel +b (ban), +e (exception), or +I (invex).
 *
 * The channel owns these nodes and their fixed-size mask strings. MODE policy
 * controls who may add/remove entries; channel_policy.c performs matching.
 */
typedef struct ChannelMaskEntry {
    char mask[IRC_CHANNEL_MASK_MAX + 1U]; /**< nick!user@host style mask. */
    struct ChannelMaskEntry *next;        /**< Next entry in the same list. */
} ChannelMaskEntry;

/**
 * One explicit, transient invitation.
 *
 * Invitations are keyed by Client.id rather than nickname so a nickname change
 * cannot accidentally invalidate or transfer an invitation.  JOIN consumes
 * the invitation after a successful entry into the channel.
 */
typedef struct ChannelInvite {
    uint64_t client_id;                    /**< Stable connection ID invited. */
    struct ChannelInvite *next;            /**< Next explicit invitation. */
} ChannelInvite;

/**
 * One member in a channel's singly linked membership list.
 *
 * Privileges are stored here because owner/op/halfop/voice apply to a client's
 * membership in one channel, not to the client globally.
 */
typedef struct ChannelMember {
    Client *client;                         /**< Client represented by this member. */
    ChannelPrivilegeSet privileges;         /**< +q/+o/+h/+v membership privileges. */
    struct ChannelMember *next;             /**< Next channel member. */
} ChannelMember;

/**
 * State for one IRC channel.
 *
 * Channel-wide boolean modes use modes. Modes that carry parameters or lists
 * have dedicated fields so their semantics remain explicit and type-safe.
 * Channel objects are owned by Server.channels_by_name.
 */
typedef struct Channel {
    char name[IRC_CHANNEL_NAME_MAX + 1U];       /**< Canonical spelling from creation. */
    ChannelModeSet modes;                       /**< Boolean channel mode bits. */

    char key[IRC_CHANNEL_KEY_MAX + 1U];         /**< +k join key, empty when unset. */
    size_t user_limit;                          /**< +l maximum clients, 0 when unset. */
    unsigned int join_throttle_count;           /**< +j joins allowed in interval. */
    unsigned int join_throttle_seconds;         /**< +j interval length in seconds. */
    char limit_redirect[IRC_CHANNEL_NAME_MAX + 1U]; /**< +L overflow redirect target. */
    char ban_redirect[IRC_CHANNEL_NAME_MAX + 1U];   /**< +B banned-user redirect target. */

    ChannelMaskEntry *ban_list;                 /**< +b masks. */
    ChannelMaskEntry *exception_list;           /**< +e masks. */
    ChannelMaskEntry *invite_exception_list;    /**< +I masks. */
    ChannelInvite *invites;                     /**< One-use explicit INVITE entries. */

    ChannelMember *members;                     /**< Head of member list. */
    size_t member_count;                        /**< Current number of members. */
} Channel;

/** Allocate an empty channel using name. */
Channel *channel_create(const char *name);

/** Free a Channel and all channel-owned mode/membership nodes. */
void channel_free(void *ptr);

/** Find and return client's canonical membership node, or NULL. */
ChannelMember *channel_find_member(const Channel *channel, const Client *client);

/** Return non-zero when client is a member of channel. */
int channel_has_client(const Channel *channel, const Client *client);

/** Add a bidirectional client/channel membership with no privileges. */
int channel_add_client(Channel *channel, Client *client);

/** Remove the bidirectional membership link, if present. */
void channel_remove_client(Channel *channel, Client *client);

/** Replace one member's complete +q/+o/+h/+v privilege bitset. */
int channel_set_privileges(Channel *channel, Client *client,
                           ChannelPrivilegeSet privileges);

/** Add privilege bits to one channel membership. */
int channel_add_privileges(Channel *channel, Client *client,
                           ChannelPrivilegeSet privileges);

/** Remove privilege bits from one channel membership. */
int channel_remove_privileges(Channel *channel, Client *client,
                              ChannelPrivilegeSet privileges);

/** Broadcast a complete IRC message to all members except optional except. */
void channel_broadcast(Channel *channel, const Client *except, const char *message);

/** Add mask to one channel mask list. Duplicate exact masks are ignored. */
int channel_mask_add(ChannelMaskEntry **list, const char *mask);

/** Remove exact mask from one channel mask list. Returns non-zero if removed. */
int channel_mask_remove(ChannelMaskEntry **list, const char *mask);

/** Free every entry in a channel mask list and set the list head to NULL. */
void channel_mask_clear(ChannelMaskEntry **list);

#endif /* IRCD_CHANNEL_H */
