/**
 * @file ban_db.c
 * @brief SQLite persistence for KLINE and ZLINE records.
 */

#include "ban_db.h"

#include <string.h>

static const char *schema_sql =
    "CREATE TABLE IF NOT EXISTS bans ("
    "type INTEGER NOT NULL,"
    "mask TEXT COLLATE NOCASE NOT NULL,"
    "reason TEXT NOT NULL DEFAULT '',"
    "set_by TEXT NOT NULL DEFAULT '',"
    "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
    "PRIMARY KEY(type,mask)"
    ");";

int ban_db_open(BanDb *db, const char *path) {
    char *error = NULL;
    if (db == NULL || path == NULL || *path == '\0') return -1;
    memset(db, 0, sizeof(*db));
    if (sqlite3_open(path, &db->handle) != SQLITE_OK) {
        ban_db_close(db);
        return -1;
    }
    if (sqlite3_exec(db->handle, schema_sql, NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        ban_db_close(db);
        return -1;
    }
    return 0;
}

void ban_db_close(BanDb *db) {
    if (db != NULL && db->handle != NULL) {
        sqlite3_close(db->handle);
        db->handle = NULL;
    }
}

int ban_db_add(BanDb *db, BanType type, const char *mask,
               const char *reason, const char *set_by) {
    static const char sql[] =
        "INSERT OR REPLACE INTO bans(type,mask,reason,set_by,created_at) "
        "VALUES(?1,?2,?3,?4,unixepoch())";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || mask == NULL || *mask == '\0') return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    sqlite3_bind_text(stmt, 2, mask, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, reason != NULL ? reason : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, set_by != NULL ? set_by : "", -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int ban_db_delete(BanDb *db, BanType type, const char *mask) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || mask == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle,
            "DELETE FROM bans WHERE type=?1 AND mask=?2", -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    sqlite3_bind_text(stmt, 2, mask, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

int ban_db_list(BanDb *db, BanType type, BanDbListCallback callback, void *context) {
    static const char sql[] =
        "SELECT type,mask,reason,set_by,created_at FROM bans "
        "WHERE type=?1 ORDER BY created_at,mask COLLATE NOCASE";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || callback == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        BanRecord record;
        const unsigned char *text;
        memset(&record, 0, sizeof(record));
        record.type = (BanType)sqlite3_column_int(stmt, 0);
        text = sqlite3_column_text(stmt, 1);
        if (text != NULL) snprintf(record.mask, sizeof(record.mask), "%s", (const char *)text);
        text = sqlite3_column_text(stmt, 2);
        if (text != NULL) snprintf(record.reason, sizeof(record.reason), "%s", (const char *)text);
        text = sqlite3_column_text(stmt, 3);
        if (text != NULL) snprintf(record.set_by, sizeof(record.set_by), "%s", (const char *)text);
        record.created_at = sqlite3_column_int64(stmt, 4);
        if (callback(&record, context) != 0) {
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}
