#ifndef IRCD_PRESENCE_H
#define IRCD_PRESENCE_H

/**
 * @file presence.h
 * @brief Runtime SILENCE, WATCH, and WHOWAS support.
 *
 * These facilities are intentionally in-memory IRC session state. They are not
 * service/account persistence and therefore are not stored in SQLite.
 */

#include "server.h"

/** Add/remove one SILENCE mask. Return 1 changed, 0 already/absent, -1 error/full. */
int presence_silence_add(Client *client, const char *mask);
int presence_silence_remove(Client *client, const char *mask);
int presence_silence_matches(const Client *recipient, const Client *sender);

/** Add/remove/test one WATCH nickname. */
int presence_watch_add(Client *client, const char *nick);
int presence_watch_remove(Client *client, const char *nick);
int presence_watch_contains(const Client *client, const char *nick);

/** Notify every watcher that a registered nickname became online/offline. */
void presence_watch_online(Server *server, const Client *subject);
void presence_watch_offline(Server *server, const Client *subject,
                            const char *nick_override);

/** Save one historical nick identity in the server WHOWAS ring. */
void presence_whowas_record(Server *server, const Client *client,
                            const char *nick_override);

/** Free per-client WATCH/SILENCE allocations. */
void presence_client_clear(Client *client);

#endif /* IRCD_PRESENCE_H */
