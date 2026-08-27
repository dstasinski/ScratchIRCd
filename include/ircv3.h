#ifndef IRCD_IRCV3_H
#define IRCD_IRCV3_H

#include "channel.h"
#include "client.h"

/** Notify capable peers sharing a channel that this client's account changed. */
void ircv3_account_notify(Client *client);

/**
 * Return nonzero when the untagged portion of a relayed source message fits
 * IRC's 510-byte content limit. IRCv3 server tags use their separate tag
 * allowance and therefore do not reduce this budget.
 */
int ircv3_message_wire_fits(const Client *source, const char *command,
                            const char *target, const char *text);

/**
 * Deliver one live IRC message to a single client. When the recipient has
 * negotiated server-time, a UTC time tag is added without changing the source
 * identity or message payload.
 */
void ircv3_send_message(Client *recipient, const Client *source,
                        const char *command, const char *target,
                        const char *text);

/**
 * Broadcast one live IRC message to channel members other than `except`, using
 * per-recipient IRCv3 formatting so server-time clients receive time tags.
 */
void ircv3_broadcast_message(Channel *channel, const Client *except,
                             const Client *source, const char *command,
                             const char *target, const char *text);

#endif /* IRCD_IRCV3_H */
