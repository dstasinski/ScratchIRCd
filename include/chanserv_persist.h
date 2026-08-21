#ifndef IRCD_CHANSERV_PERSIST_H
#define IRCD_CHANSERV_PERSIST_H

/**
 * @file chanserv_persist.h
 * @brief Persistent parameter modes and channel mask lists for ChanServ.
 *
 * These helpers store runtime channel policy in companion tables inside the
 * configured ChanServ SQLite database. They deliberately do not own channel
 * registration metadata or access lists; those remain in chanserv_db.c.
 */

#include "channel.h"

/** Ensure the companion persistence tables exist. */
int chanserv_persist_init(const char *path);

/** Restore persistent parameter modes and +b/+e/+I lists into a live channel. */
int chanserv_persist_restore(const char *path, Channel *channel);

/** Save the live channel's parameter modes and +b/+e/+I lists atomically. */
int chanserv_persist_save(const char *path, const Channel *channel);

#endif /* IRCD_CHANSERV_PERSIST_H */
