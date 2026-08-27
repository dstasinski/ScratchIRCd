#ifndef IRCD_SQLITE_POLICY_H
#define IRCD_SQLITE_POLICY_H

#include <sqlite3.h>

/*
 * ScratchIRCd is intentionally single-server and most database work executes
 * on the IRC event-loop thread.  A long SQLite busy wait therefore stalls all
 * clients, not just the command which touched the database.  Keep lock waits
 * short and use WAL so readers do not unnecessarily block the single writer.
 */
#define IRCD_SQLITE_BUSY_TIMEOUT_MS 250

static inline int ircd_sqlite_open_policy(const char *filename, sqlite3 **handle) {
    int rc;
    if (filename == NULL || handle == NULL) return SQLITE_MISUSE;
    rc = sqlite3_open_v2(filename, handle,
                         SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                         NULL);
    if (rc != SQLITE_OK || *handle == NULL) return rc;

    (void)sqlite3_busy_timeout(*handle, IRCD_SQLITE_BUSY_TIMEOUT_MS);

    /* These PRAGMAs are best-effort.  Existing databases remain usable if a
     * filesystem cannot support WAL; the short busy timeout still applies. */
    (void)sqlite3_exec(*handle, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    (void)sqlite3_exec(*handle, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    (void)sqlite3_exec(*handle, "PRAGMA wal_autocheckpoint=1000;", NULL, NULL, NULL);
    return SQLITE_OK;
}

/* Route project opens through the common policy without rewriting every DB
 * module.  Existing explicit busy_timeout calls are clamped to the same
 * event-loop-safe ceiling below. */
#define sqlite3_open(filename, handle) \
    ircd_sqlite_open_policy((filename), (handle))
#define sqlite3_busy_timeout(handle, milliseconds) \
    sqlite3_busy_timeout((handle), IRCD_SQLITE_BUSY_TIMEOUT_MS)

#endif /* IRCD_SQLITE_POLICY_H */
