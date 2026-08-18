#ifndef IRCD_CHANNEL_POLICY_H
#define IRCD_CHANNEL_POLICY_H

/**
 * @file channel_policy.h
 * @brief Shared channel-access helpers used by JOIN, INVITE, MODE and services.
 *
 * This module centralizes IRC wildcard mask matching and transient invitation
 * state so command handlers do not each grow their own subtly different
 * versions of channel access policy.
 */

#include <stdint.h>

#include "channel.h"

/** Return non-zero when text matches an RFC1459-casemapped '*'/'?' pattern. */
int irc_mask_match(const char *pattern, const char *text);

/** Return non-zero if client matches at least one mask in list. */
int channel_mask_matches_client(const ChannelMaskEntry *list,
                                const Client *client);

/** Return non-zero when client is banned and not covered by a +e exception. */
int channel_client_is_banned(const Channel *channel, const Client *client);

/** Return non-zero when client matches the channel +I invite-exception list. */
int channel_client_is_invex(const Channel *channel, const Client *client);

/** Add a one-use invitation for a stable client connection ID. */
int channel_invite_add(Channel *channel, uint64_t client_id);

/** Return non-zero if the client ID currently has an explicit invitation. */
int channel_invite_has(const Channel *channel, uint64_t client_id);

/** Consume one invitation, returning non-zero when one existed. */
int channel_invite_consume(Channel *channel, uint64_t client_id);

/** Remove all transient invitations from channel. */
void channel_invite_clear(Channel *channel);

#endif /* IRCD_CHANNEL_POLICY_H */
