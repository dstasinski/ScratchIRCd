/**
 * @file chanserv_db_logging.c
 * @brief ChanServ persistence for optional per-channel logging.
 */

#include "chanserv_db.h"

#include <stdio.h>
#include <string.h>

static int column_exists(sqlite3 *db, const char *column) {
    sqlite3_stmt *stmt = NULL;
    int found = 0;

    if (db == NULL || column == NULL) return 0;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(channels)", -1,
                           &stmt, NULL) != SQLITE_OK)
        return 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name != NULL && strcmp(name, column) == 0) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

int chanserv_db_logging_ensure_schema(ChanServDb *db) {
    char *error = NULL;
    int rc;

    if (db == NULL || db->db == NULL) return -1;
    if (column_exists(db->db, "logging_enabled")) return 0;

    rc = sqlite3_exec(db->db,
        "ALTER TABLE channels ADD COLUMN logging_enabled INTEGER NOT NULL DEFAULT 0",
        NULL, NULL, &error);
    if (rc != SQLITE_OK) {
        if (error != NULL)
            fprintf(stderr, "ChanServ DB logging migration: %s\n", error);
        sqlite3_free(error);
        return -1;
    }
    return 0;
}

int chanserv_db_logging_get(ChanServDb *db, const char *name,
                            int *registered, int *enabled) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (registered != NULL) *registered = 0;
    if (enabled != NULL) *enabled = 0;
    if (db == NULL || db->db == NULL || name == NULL) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;

    if (sqlite3_prepare_v2(db->db,
        "SELECT enabled,logging_enabled FROM channels WHERE name=?1",
        -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int channel_enabled = sqlite3_column_int(stmt, 0) != 0;
        if (registered != NULL) *registered = channel_enabled;
        if (enabled != NULL)
            *enabled = channel_enabled && sqlite3_column_int(stmt, 1) != 0;
        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_db_logging_set(ChanServDb *db, const char *name, int enabled) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (db == NULL || db->db == NULL || name == NULL) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;

    if (sqlite3_prepare_v2(db->db,
        "UPDATE channels SET logging_enabled=?1,updated_at=unixepoch() "
        "WHERE name=?2 AND enabled=1",
        -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE && sqlite3_changes(db->db) > 0 ? 0 : -1;
}
