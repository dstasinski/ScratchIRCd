#ifndef IRCD_IRCV3_H
#define IRCD_IRCV3_H

#include "channel.h"
#include "client.h"

struct Server;

/** Parse one client tag section and establish per-command IRCv3 context. */
void ircv3_begin_command(struct Server *server, Client *client, char *tags);

/** Close a labeled response, if active, and discard per-command client tags. */
void ircv3_end_command(Client *client);

/** Notify capable peers sharing a channel that this client's account changed. */
void ircv3_account_notify(Client *client);

/** Notify capable channel peers that a client changed away state. */
void ircv3_away_notify(Client *client);

/** Send the joining client's existing away state to capable channel peers. */
void ircv3_away_notify_join(Channel *channel, const Client *client);

/** Broadcast JOIN using traditional or extended-join syntax per recipient. */
void ircv3_broadcast_join(Channel *channel, const Client *client);

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

/** Deliver a client-tag-only TAGMSG to one capable recipient. */
void ircv3_send_tagmsg(Client *recipient, const Client *source,
                       const char *target);

/** Broadcast a client-tag-only TAGMSG to capable channel members. */
void ircv3_broadcast_tagmsg(Channel *channel, const Client *except,
                            const Client *source, const char *target);

#endif /* IRCD_IRCV3_H */
