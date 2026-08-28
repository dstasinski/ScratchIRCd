/**
 * @file operator_db.c
 * @brief SQLite implementation of persistent IRC operator accounts.
 *
 * The schema is intentionally created with the exact columns requested by the
 * project. All mutable account fields refresh updated_at using unixepoch().
 */

#include "operator_db.h"
#include "sqlite_policy.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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

static int text_fits(const char *text, size_t max_length, int allow_empty) {
    size_t length;
    if (text == NULL) return 0;
    length = strnlen(text, max_length + 1U);
    if (length > max_length) return 0;
    return allow_empty || length != 0U;
}

static int copy_text_column(sqlite3_stmt *stmt, int column,
                            char *destination, size_t destination_size) {
    const unsigned char *text;
    int bytes;
    if (stmt == NULL || destination == NULL || destination_size == 0U) return -1;
    text = sqlite3_column_text(stmt, column);
    bytes = sqlite3_column_bytes(stmt, column);
    if (text == NULL || bytes < 0 || (size_t)bytes >= destination_size) return -1;
    if (bytes != 0 && memchr(text, '\0', (size_t)bytes) != NULL) return -1;
    memcpy(destination, text, (size_t)bytes);
    destination[bytes] = '\0';
    return 0;
}

static int persisted_vhosts_valid(sqlite3 *handle) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (handle == NULL) return -1;
    if (sqlite3_prepare_v2(handle,
            "SELECT 1 FROM operators WHERE length(CAST(vhost AS BLOB))>?1 LIMIT 1",
            -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)IRC_HOST_MAX);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

/*
 * Ensure the immediate parent directory for a configured database path exists.
 * ScratchIRCd's standard database layout is data/<database>.db, so one parent
 * level is sufficient for the supported configuration. Existing directories
 * are accepted. Absolute paths and explicitly configured alternate directories
 * work as long as their own parent already exists.
 */
static int ensure_parent_directory(const char *path) {
    char parent[IRCD_CONFIG_PATH_MAX + 1U];
    char *slash;
    size_t length;

    if (path == NULL) return -1;
    length = strlen(path);
    if (length == 0U || length >= sizeof(parent)) return -1;

    (void)snprintf(parent, sizeof(parent), "%s", path);
    slash = strrchr(parent, '/');
    if (slash == NULL) return 0;
    if (slash == parent) return 0; /* root directory */
    *slash = '\0';

    if (mkdir(parent, 0750) == 0 || errno == EEXIST) return 0;
    return -1;
}

int operator_db_open(OperatorDb *db, const char *path) {
    char *error = NULL;

    if (db == NULL || path == NULL || *path == '\0') return -1;
    memset(db, 0, sizeof(*db));

    if (ensure_parent_directory(path) != 0) return -1;

    if (sqlite3_open(path, &db->handle) != SQLITE_OK) {
        operator_db_close(db);
        return -1;
    }
    if (ircd_sqlite_apply_policy(db->handle) != 0) {
        operator_db_close(db);
        return -1;
    }

    if (sqlite3_exec(db->handle, schema_sql, NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        operator_db_close(db);
        return -1;
    }
    if (persisted_vhosts_valid(db->handle) != 0) {
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

static int record_from_stmt(sqlite3_stmt *stmt, OperatorRecord *record) {
    if (stmt == NULL || record == NULL) return -1;
    memset(record, 0, sizeof(*record));
    if (copy_text_column(stmt, 0, record->name, sizeof(record->name)) != 0 ||
        copy_text_column(stmt, 1, record->password_hash, sizeof(record->password_hash)) != 0 ||
        copy_text_column(stmt, 2, record->permissions, sizeof(record->permissions)) != 0 ||
        copy_text_column(stmt, 3, record->vhost, sizeof(record->vhost)) != 0 ||
        record->name[0] == '\0' || record->password_hash[0] == '\0') {
        memset(record, 0, sizeof(*record));
        return -1;
    }
    record->enabled = sqlite3_column_int(stmt, 4);
    record->created_at = sqlite3_column_int64(stmt, 5);
    record->updated_at = sqlite3_column_int64(stmt, 6);
    return 0;
}

static int name_valid(const char *name) {
    return text_fits(name, IRCD_OPER_NAME_MAX, 0);
}

int operator_db_get(OperatorDb *db, const char *name, OperatorRecord *record) {
    static const char sql[] =
        "SELECT name,password_hash,permissions,vhost,enabled,created_at,updated_at "
        "FROM operators WHERE name=?1";
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (db == NULL || db->handle == NULL || !name_valid(name) || record == NULL) return -1;
    memset(record, 0, sizeof(*record));
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    (void)sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (record_from_stmt(stmt, record) != 0) {
            sqlite3_finalize(stmt);
            return -1;
        }
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int operator_db_add(OperatorDb *db, const OperatorRecord *record) {
    static const char sql[] =
        "INSERT INTO operators(name,password_hash,permissions,vhost,enabled) "
        "VALUES(?1,?2,?3,?4,?5)";
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (db == NULL || db->handle == NULL || record == NULL ||
        !name_valid(record->name) ||
        !text_fits(record->password_hash, IRCD_OPER_HASH_MAX, 0) ||
        !text_fits(record->permissions, IRCD_OPER_FLAGS_MAX, 1) ||
        !text_fits(record->vhost, IRCD_OPER_VHOST_MAX, 1)) return -1;
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
    if (db == NULL || db->handle == NULL || !name_valid(name)) return -1;
    if (sqlite3_prepare_v2(db->handle, "DELETE FROM operators WHERE name=?1", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

static int update_text(OperatorDb *db, const char *sql, const char *name,
                       const char *value, size_t value_max, int allow_empty) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || sql == NULL || !name_valid(name) ||
        !text_fits(value, value_max, allow_empty)) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

int operator_db_set_name(OperatorDb *db, const char *name, const char *new_name) {
    return update_text(db,
        "UPDATE operators SET name=?1,updated_at=unixepoch() WHERE name=?2",
        name, new_name, IRCD_OPER_NAME_MAX, 0);
}

int operator_db_set_password(OperatorDb *db, const char *name, const char *password_hash) {
    return update_text(db,
        "UPDATE operators SET password_hash=?1,updated_at=unixepoch() WHERE name=?2",
        name, password_hash, IRCD_OPER_HASH_MAX, 0);
}

int operator_db_set_permissions(OperatorDb *db, const char *name, const char *permissions) {
    return update_text(db,
        "UPDATE operators SET permissions=?1,updated_at=unixepoch() WHERE name=?2",
        name, permissions, IRCD_OPER_FLAGS_MAX, 1);
}

int operator_db_set_vhost(OperatorDb *db, const char *name, const char *vhost) {
    return update_text(db,
        "UPDATE operators SET vhost=?1,updated_at=unixepoch() WHERE name=?2",
        name, vhost, IRCD_OPER_VHOST_MAX, 1);
}

int operator_db_set_enabled(OperatorDb *db, const char *name, int enabled) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || !name_valid(name)) return -1;
    if (sqlite3_prepare_v2(db->handle,
            "UPDATE operators SET enabled=?1,updated_at=unixepoch() WHERE name=?2",
            -1, &stmt, NULL) != SQLITE_OK) return -1;
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
            "SELECT name,password_hash,permissions,vhost,enabled,created_at,updated_at "
            "FROM operators ORDER BY name COLLATE NOCASE",
            -1, &stmt, NULL) != SQLITE_OK) return -1;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        OperatorRecord record;
        if (record_from_stmt(stmt, &record) != 0) {
            sqlite3_finalize(stmt);
            return -1;
        }
        if (callback(&record, context) != 0) {
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}
