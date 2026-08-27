#ifndef IRCD_SQLITE_POLICY_H
#define IRCD_SQLITE_POLICY_H

#include <sqlite3.h>

#define IRCD_SQLITE_BUSY_TIMEOUT_MS 250

/*
 * Apply ScratchIRCd's common SQLite connection policy immediately after open.
 * The short busy ceiling bounds single-threaded event-loop stalls. WAL is
 * preferred but deliberately best-effort because some SQLite targets (for
 * example in-memory databases or filesystems without WAL support) may retain a
 * different journal mode. synchronous=NORMAL is likewise applied when the
 * connection supports it.
 */
static inline int ircd_sqlite_apply_policy(sqlite3 *db) {
    if (db == NULL) return -1;
    if (sqlite3_busy_timeout(db, IRCD_SQLITE_BUSY_TIMEOUT_MS) != SQLITE_OK)
        return -1;
    (void)sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    if (sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL) != SQLITE_OK)
        return -1;
    return 0;
}

#endif /* IRCD_SQLITE_POLICY_H */
