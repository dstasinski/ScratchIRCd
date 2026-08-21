/** @file chanserv_db.c @brief SQLite persistence for registered channels. */
#include "chanserv_db.h"
#include <stdio.h>
#include <string.h>

static int exec_sql(sqlite3 *db, const char *sql) {
    char *error = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &error);
    if (rc != SQLITE_OK) {
        if (error != NULL) fprintf(stderr, "ChanServ DB: %s\n", error);
        sqlite3_free(error);
        return -1;
    }
    return 0;
}

int chanserv_db_open(ChanServDb *db, const char *path) {
    static const char schema[] =
        "CREATE TABLE IF NOT EXISTS channels ("
        "name TEXT COLLATE NOCASE PRIMARY KEY,"
        "founder TEXT COLLATE NOCASE NOT NULL,"
        "description TEXT NOT NULL DEFAULT '',"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
        ");"
        "CREATE INDEX IF NOT EXISTS channels_founder_idx ON channels(founder);";
    if (db == NULL || path == NULL) return -1;
    memset(db, 0, sizeof(*db));
    if (sqlite3_open(path, &db->db) != SQLITE_OK) {
        chanserv_db_close(db);
        return -1;
    }
    sqlite3_busy_timeout(db->db, 2000);
    if (exec_sql(db->db, "PRAGMA foreign_keys=ON;") != 0 || exec_sql(db->db, schema) != 0) {
        chanserv_db_close(db);
        return -1;
    }
    return 0;
}

void chanserv_db_close(ChanServDb *db) {
    if (db != NULL && db->db != NULL) sqlite3_close(db->db);
    if (db != NULL) db->db = NULL;
}

int chanserv_db_get(ChanServDb *db, const char *name, ChanServChannel *record) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || name == NULL || record == NULL) return -1;
    memset(record, 0, sizeof(*record));
    if (sqlite3_prepare_v2(db->db,
        "SELECT name,founder,description,enabled,created_at,updated_at FROM channels WHERE name=?1",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        (void)snprintf(record->name, sizeof(record->name), "%s", sqlite3_column_text(stmt, 0));
        (void)snprintf(record->founder, sizeof(record->founder), "%s", sqlite3_column_text(stmt, 1));
        (void)snprintf(record->description, sizeof(record->description), "%s", sqlite3_column_text(stmt, 2));
        record->enabled = sqlite3_column_int(stmt, 3);
        record->created_at = sqlite3_column_int64(stmt, 4);
        record->updated_at = sqlite3_column_int64(stmt, 5);
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_db_create(ChanServDb *db, const char *name, const char *founder, const char *description) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || name == NULL || founder == NULL) return -1;
    if (sqlite3_prepare_v2(db->db,
        "INSERT INTO channels(name,founder,description) VALUES(?1,?2,?3)",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, founder, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, description != NULL ? description : "", -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int update_text(ChanServDb *db, const char *name, const char *column, const char *value) {
    char sql[160];
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || name == NULL || value == NULL) return -1;
    (void)snprintf(sql, sizeof(sql), "UPDATE channels SET %s=?1,updated_at=unixepoch() WHERE name=?2", column);
    if (sqlite3_prepare_v2(db->db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->db) > 0 ? 0 : -1;
}

int chanserv_db_set_description(ChanServDb *db, const char *name, const char *description) {
    return update_text(db, name, "description", description);
}
int chanserv_db_set_founder(ChanServDb *db, const char *name, const char *founder) {
    return update_text(db, name, "founder", founder);
}

int chanserv_db_set_enabled(ChanServDb *db, const char *name, int enabled) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || name == NULL) return -1;
    if (sqlite3_prepare_v2(db->db,
        "UPDATE channels SET enabled=?1,updated_at=unixepoch() WHERE name=?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->db) > 0 ? 0 : -1;
}

int chanserv_db_delete(ChanServDb *db, const char *name) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || name == NULL) return -1;
    if (sqlite3_prepare_v2(db->db, "DELETE FROM channels WHERE name=?1", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->db) > 0 ? 0 : -1;
}

int chanserv_db_list_enabled(ChanServDb *db, char *buffer, size_t size) {
    sqlite3_stmt *stmt = NULL;
    size_t used = 0U;
    int rc;
    if (db == NULL || db->db == NULL || buffer == NULL || size == 0U) return -1;
    buffer[0] = '\0';
    if (sqlite3_prepare_v2(db->db,
        "SELECT name FROM channels WHERE enabled=1 ORDER BY name COLLATE NOCASE",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        int written = snprintf(buffer + used, size - used, "%s%s", used != 0U ? "," : "", name);
        if (written < 0 || (size_t)written >= size - used) break;
        used += (size_t)written;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE || rc == SQLITE_ROW ? 0 : -1;
}
