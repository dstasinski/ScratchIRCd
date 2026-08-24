#ifndef IRCD_CHANNEL_POLICY_H
#define IRCD_CHANNEL_POLICY_H

/**
 * @file channel_policy.h
 * @brief Shared channel-access helpers used by JOIN, INVITE, MODE and services.
 */

#include <stdint.h>

#include "channel.h"

int irc_mask_match(const char *pattern, const char *text);
int channel_mask_matches_client(const ChannelMaskEntry *list,
                                const Client *client);
int channel_client_is_banned(const Channel *channel, const Client *client);
/** Evaluate only bans authorized by a PROTECTED or OWNER setter. */
int channel_client_is_banned_protected(const Channel *channel,
                                       const Client *client);
int channel_client_is_invex(const Channel *channel, const Client *client);

int channel_invite_add(Channel *channel, uint64_t client_id);
int channel_invite_has(const Channel *channel, uint64_t client_id);
int channel_invite_consume(Channel *channel, uint64_t client_id);
void channel_invite_clear(Channel *channel);

/** Return non-zero if channel-wide +j currently permits another join. */
int channel_join_throttle_allows(Channel *channel, uint64_t client_id);

/** Record one successful join in the channel-wide +j window. */
void channel_join_throttle_record(Channel *channel, uint64_t client_id);

/** Reset all +j window accounting. */
void channel_join_throttle_clear(Channel *channel);

#endif /* IRCD_CHANNEL_POLICY_H */
