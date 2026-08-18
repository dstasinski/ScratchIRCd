#ifndef IRCD_VISIBILITY_H
#define IRCD_VISIBILITY_H

/**
 * @file visibility.h
 * @brief Shared visibility rules for LIST, NAMES, WHO, and WHOIS.
 *
 * IRC visibility is policy rather than storage.  Keeping these decisions in
 * one module prevents information-query commands from disagreeing about
 * private/secret channels, '&' channels, invisible users, or operator access.
 */

#include "channel.h"
#include "client.h"

/** Return non-zero when requester has IRC operator or network-admin status. */
int visibility_is_oper(const Client *requester);

/** Return non-zero when requester and subject share at least one channel. */
int visibility_share_channel(const Client *requester, const Client *subject);

/** Return non-zero when channel should appear in an unqualified LIST. */
int visibility_list_channel(const Client *requester, const Channel *channel);

/** Return non-zero when requester may see channel membership through NAMES. */
int visibility_names_channel(const Client *requester, const Channel *channel);

/** Return non-zero when subject may appear in an unqualified/general WHO. */
int visibility_who_user(const Client *requester, const Client *subject);

/** Return non-zero when one channel may be exposed in subject's WHOIS list. */
int visibility_whois_channel(const Client *requester, const Client *subject,
                             const Channel *channel);

#endif /* IRCD_VISIBILITY_H */
