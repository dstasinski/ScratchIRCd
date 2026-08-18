/**
 * @file operator_db.c
 * @brief SQLite implementation of persistent IRC operator accounts.
 *
 * The schema is intentionally created exactly as specified by the project.
 * All update helpers also refresh updated_at using SQLite's unixepoch().
 */

#include "operator_db.h"

#include <stdio.h>
#include <string.h>

static const char *schema_sql =
    "CREATE TABLE IF NOT EXISTS \"operators\" ("
    "\"name\" TEXT COLLATE NOCASE,"
    "\"password_hash\" TEXT NOT NULL,"
    "\"permissions\" TEXT NOT NULL DEFAULT '',"
    "\"vhost\" TEXT NOT NULL,"
    "\"enabled\" INTEGER NOT NULL DEFAULT 1,"
    "\"created_at\" INTEGER NOT NULL DEFAULT (unixepoch()),"
    "\"updated_at\" INTEGER NOT NULL DEFAULT (unixepoch()),"
    "PRIMARY KEY(\"name\")"
    ");";

static void copy_text(char *dest, size_t size, const unsigned char *value) {
    (void)snprintf(dest, size, "%s", value != NULL ? (const char *)value : "");
}

int operator_db_open(OperatorDb *db, const char *path) {
    char *error = NULL;

    if (db == NULL || path == NULL || *path == '\0') return -1;
    memset(db, 0, sizeof(*db));

    if (sqlite3_open(path, &db->handle) != SQLITE_OK) {
        operator_db_close(db);
        return -1;
    }

    if (sqlite3_exec(db->handle, schema_sql, NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        operator_db_close(db);
        return -1;
    }
    return 0;
}

void operator_db_close(OperatorDb *db) {
    if (db != NULL && db->handle != NULL) {
        sqlite3_close(db->handle);
        db->handle = NULL;
    }
}

static void record_from_stmt(sqlite3_stmt *stmt, OperatorRecord *record) {
    memset(record, 0, sizeof(*record));
    copy_text(record->name, sizeof(record->name), sqlite3_column_text(stmt, 0));
    copy_text(record->password_hash, sizeof(record->password_hash), sqlite3_column_text(stmt, 1));
    copy_text(record->permissions, sizeof(record->permissions), sqlite3_column_text(stmt, 2));
    copy_text(record->vhost, sizeof(record->vhost), sqlite3_column_text(stmt, 3));
    record->enabled = sqlite3_column_int(stmt, 4);
    record->created_at = sqlite3_column_int64(stmt, 5);
    record->updated_at = sqlite3_column_int64(stmt, 6);
}

int operator_db_get(OperatorDb *db, const char *name, OperatorRecord *record) {
    static const char sql[] =
        "SELECT name,password_hash,permissions,vhost,enabled,created_at,updated_at "
        "FROM operators WHERE name=?1";
    sqlite3_stmt *stmt = NULL;
    int result = -1;

    if (db == NULL || db->handle == NULL || name == NULL || record == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    (void)sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        record_from_stmt(stmt, record);
        result = 1;
    } else {
        result = 0;
    }
    sqlite3_finalize(stmt);
    return result;
}

int operator_db_add(OperatorDb *db, const OperatorRecord *record) {
    static const char sql[] =
        "INSERT INTO operators(name,password_hash,permissions,vhost,enabled) "
        "VALUES(?1,?2,?3,?4,?5)";
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (db == NULL || db->handle == NULL || record == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, record->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record->password_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, record->permissions, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, record->vhost, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, record->enabled ? 1 : 0);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int operator_db_delete(OperatorDb *db, const char *name) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, "DELETE FROM operators WHERE name=?1", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

static int update_text(OperatorDb *db, const char *sql, const char *name, const char *value) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL || value == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

int operator_db_set_password(OperatorDb *db, const char *name, const char *password_hash) {
    return update_text(db, "UPDATE operators SET password_hash=?1,updated_at=unixepoch() WHERE name=?2", name, password_hash);
}

int operator_db_set_permissions(OperatorDb *db, const char *name, const char *permissions) {
    return update_text(db, "UPDATE operators SET permissions=?1,updated_at=unixepoch() WHERE name=?2", name, permissions);
}

int operator_db_set_vhost(OperatorDb *db, const char *name, const char *vhost) {
    return update_text(db, "UPDATE operators SET vhost=?1,updated_at=unixepoch() WHERE name=?2", name, vhost);
}

int operator_db_set_enabled(OperatorDb *db, const char *name, int enabled) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, "UPDATE operators SET enabled=?1,updated_at=unixepoch() WHERE name=?2", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

int operator_db_list(OperatorDb *db, OperatorDbListCallback callback, void *context) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (db == NULL || db->handle == NULL || callback == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle,
            "SELECT name,password_hash,permissions,vhost,enabled,created_at,updated_at FROM operators ORDER BY name COLLATE NOCASE",
            -1, &stmt, NULL) != SQLITE_OK) return -1;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        OperatorRecord record;
        record_from_stmt(stmt, &record);
        if (callback(&record, context) != 0) break;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE || rc == SQLITE_ROW ? 0 : -1;
}
