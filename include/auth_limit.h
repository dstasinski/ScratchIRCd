#ifndef IRCD_AUTH_LIMIT_H
#define IRCD_AUTH_LIMIT_H

#include "server.h"

/**
 * Consume one client-triggered password-hashing/verification work unit.
 * Returns non-zero when the expensive Argon2 operation may proceed.
 * State is bounded, memory-only, and keyed by the client's real IP.
 */
int auth_limit_consume(Server *server, Client *client, const char *operation);

#endif /* IRCD_AUTH_LIMIT_H */
