#ifndef IRCD_NICKSERV_H
#define IRCD_NICKSERV_H

/**
 * @file nickserv.h
 * @brief Virtual NickServ service and account-authentication helpers.
 */

#include "server.h"

#include <stddef.h>
#include <strings.h>

/** Process text sent to the virtual NickServ target. */
void nickserv_handle_message(Server *server, Client *client, char *text);

/** Authenticate a client to a registered account. Returns 1 on success. */
int nickserv_identify(Server *server, Client *client,
                      const char *account_name, const char *password);

/** Close process-local NickServ service state for shutdown or in-process RESTART. */
void nickserv_reset_runtime_state(void);

/**
 * Return non-zero when a nickname is owned by an internal virtual service.
 * These names are server-only and may not be used or registered by anyone.
 * Matching is case-insensitive for all case variants of the service name.
 */
static inline int service_nickname_reserved(const char *nick) {
    if (nick == NULL) return 0;
    return strcasecmp(nick, "NickServ") == 0 ||
           strcasecmp(nick, "ChanServ") == 0 ||
           strcasecmp(nick, "MemoServ") == 0;
}

/**
 * Return non-zero when nick appears in the ircd.conf reserved_nicks list.
 * Matching is case-insensitive; configured spelling is preserved in storage.
 */
static inline int nickserv_config_reserved_nickname(const Server *server,
                                                    const char *nick) {
    size_t i;

    if (server == NULL || nick == NULL) return 0;
    for (i = 0U; i < server->config.reserved_nick_count; ++i) {
        if (strcasecmp(server->config.reserved_nicks[i], nick) == 0) return 1;
    }
    return 0;
}

/** Return non-zero when client may use/register an ircd.conf reserved nickname. */
static inline int nickserv_reserved_nickname_allowed(const Client *client) {
    return client != NULL &&
           (client_mode_has(client->modes, CLIENT_MODE_OPER) ||
            client_mode_has(client->modes, CLIENT_MODE_NETADMIN));
}

#endif /* IRCD_NICKSERV_H */
