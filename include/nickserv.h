#ifndef IRCD_NICKSERV_H
#define IRCD_NICKSERV_H

/**
 * @file nickserv.h
 * @brief Virtual NickServ service and account-authentication helpers.
 */

#include "server.h"

/** Process text sent to the virtual NickServ target. */
void nickserv_handle_message(Server *server, Client *client, char *text);

/** Authenticate a client to a registered account. Returns 1 on success. */
int nickserv_identify(Server *server, Client *client,
                      const char *account_name, const char *password);

/** Close process-local NickServ service state for shutdown or in-process RESTART. */
void nickserv_reset_runtime_state(void);

/** Return non-zero when a nickname is reserved for an internal service. */
int service_nickname_reserved(const char *nick);

#endif /* IRCD_NICKSERV_H */
