#ifndef IRCD_SQLITE3_WRAPPER_H
#define IRCD_SQLITE3_WRAPPER_H

#include <stddef.h>

/* Pull in the platform SQLite API first, then wrap only the two entry points
 * ScratchIRCd uses to establish lock-wait policy. */
#include_next <sqlite3.h>

#define IRCD_SQLITE_BUSY_TIMEOUT_MS 250

static inline int ircd_sqlite_open_policy(const char *filename, sqlite3 **handle) {
    int rc;
    if (filename == NULL || handle == NULL) return SQLITE_MISUSE;

    rc = sqlite3_open_v2(filename, handle,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                         NULL);
    if (rc != SQLITE_OK || *handle == NULL) return rc;

    /* Database work is normally performed on the single IRC event-loop
     * thread.  Never let a contended SQLite writer freeze all clients for the
     * previous 1-2 second module-specific waits. */
    (void)sqlite3_busy_timeout(*handle, IRCD_SQLITE_BUSY_TIMEOUT_MS);

    /* WAL permits readers and the single writer to coexist much better.
     * These are deliberately best-effort: unusual filesystems remain usable
     * even if WAL cannot be enabled. */
    (void)sqlite3_exec(*handle, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    (void)sqlite3_exec(*handle, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    (void)sqlite3_exec(*handle, "PRAGMA wal_autocheckpoint=1000;", NULL, NULL, NULL);
    return SQLITE_OK;
}

#define sqlite3_open(filename, handle) \
    ircd_sqlite_open_policy((filename), (handle))

/* Clamp legacy module-specific 1000/2000 ms calls to the common ceiling. */
#define sqlite3_busy_timeout(handle, milliseconds) \
    sqlite3_busy_timeout((handle), IRCD_SQLITE_BUSY_TIMEOUT_MS)

#endif /* IRCD_SQLITE3_WRAPPER_H */
