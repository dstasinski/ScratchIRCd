#ifndef IRCD_USERMODE_POLICY_H
#define IRCD_USERMODE_POLICY_H

/**
 * @file usermode_policy.h
 * @brief Behavioral helpers for ScratchIRCd user modes.
 */

#include "server.h"

/** Return non-zero when a +d recipient should still receive channel text. */
int usermode_deaf_allows_text(const Client *recipient, const char *text);

/** Apply +x by replacing only display_host with a deterministic cloak. */
void usermode_apply_cloak(const Server *server, Client *client);

/** Remove +x and restore display_host from the verified real identity. */
void usermode_remove_cloak(Client *client);

#endif /* IRCD_USERMODE_POLICY_H */
