#ifndef IRCD_MESSAGE_POLICY_H
#define IRCD_MESSAGE_POLICY_H

/**
 * @file message_policy.h
 * @brief Shared channel-message filtering and server/operator notice helpers.
 */

#include <stddef.h>

#include "server.h"

/** Return non-zero when text contains IRC or ANSI color control sequences. */
int message_contains_color(const char *text);

/**
 * Copy text while removing IRC color codes and ANSI SGR color sequences.
 * Other ordinary text and non-color IRC formatting controls are preserved.
 */
void message_strip_color(const char *text, char *out, size_t out_size);

/** Broadcast a daemon-generated server notice to registered clients with +s. */
void server_notice_broadcast(Server *server, const char *text);

/** Broadcast an operator message to registered clients with +g. */
void oper_message_broadcast(Server *server, const Client *source,
                            const char *command, const char *text);

#endif /* IRCD_MESSAGE_POLICY_H */
