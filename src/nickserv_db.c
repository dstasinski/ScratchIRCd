/**
 * @file nickserv_db.c
 * @brief SQLite persistence for registered NickServ accounts.
 */

#include "nickserv_db.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *schema_sql =
    "CREATE TABLE IF NOT EXISTS nickserv_accounts ("
    "name TEXT COLLATE NOCASE PRIMARY KEY,"
    "password_hash TEXT NOT NULL,"
    "vhost TEXT NOT NULL DEFAULT '',"
    "enabled INTEGER NOT NULL DEFAULT 1,"
    "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
    "updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
    ");";

static int ensure_parent_directory(const char *path) {
    char parent[IRCD_CONFIG_PATH_MAX + 1U];
    char *slash;
    size_t length;

    if (path == NULL) return -1;
    length = strlen(path);
    if (length == 0U || length >= sizeof(parent)) return -1;
    (void)snprintf(parent, sizeof(parent), "%s", path);
    slash = strrchr(parent, '/');
    if (slash == NULL || slash == parent) return 0;
    *slash = '\0';
    return mkdir(parent, 0750) == 0 || errno == EEXIST ? 0 : -1;
}

static void copy_text(char *dest, size_t size, const unsigned char *text) {
    (void)snprintf(dest, size, "%s", text != NULL ? (const char *)text : "");
}

static void from_stmt(sqlite3_stmt *stmt, NickServAccount *account) {
    memset(account, 0, sizeof(*account));
    copy_text(account->name, sizeof(account->name), sqlite3_column_text(stmt, 0));
    copy_text(account->password_hash, sizeof(account->password_hash), sqlite3_column_text(stmt, 1));
    copy_text(account->vhost, sizeof(account->vhost), sqlite3_column_text(stmt, 2));
    account->enabled = sqlite3_column_int(stmt, 3);
    account->created_at = sqlite3_column_int64(stmt, 4);
    account->updated_at = sqlite3_column_int64(stmt, 5);
}

int nickserv_db_open(NickServDb *db, const char *path) {
    char *error = NULL;
    if (db == NULL || path == NULL || *path == '\0') return -1;
    memset(db, 0, sizeof(*db));
    if (ensure_parent_directory(path) != 0) return -1;
    if (sqlite3_open(path, &db->handle) != SQLITE_OK) {
        nickserv_db_close(db);
        return -1;
    }
    if (sqlite3_exec(db->handle, schema_sql, NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        nickserv_db_close(db);
        return -1;
    }
    return 0;
}

void nickserv_db_close(NickServDb *db) {
    if (db != NULL && db->handle != NULL) {
        sqlite3_close(db->handle);
        db->handle = NULL;
    }
}

int nickserv_db_get(NickServDb *db, const char *name, NickServAccount *account) {
    static const char sql[] =
        "SELECT name,password_hash,vhost,enabled,created_at,updated_at "
        "FROM nickserv_accounts WHERE name=?1";
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (db == NULL || db->handle == NULL || name == NULL || account == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        from_stmt(stmt, account);
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int nickserv_db_add(NickServDb *db, const NickServAccount *account) {
    static const char sql[] =
        "INSERT INTO nickserv_accounts(name,password_hash,vhost,enabled) "
        "VALUES(?1,?2,?3,?4)";
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (db == NULL || db->handle == NULL || account == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, account->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account->password_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account->vhost, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, account->enabled ? 1 : 0);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int nickserv_db_delete(NickServDb *db, const char *name) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, "DELETE FROM nickserv_accounts WHERE name=?1", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

static int set_text(NickServDb *db, const char *sql, const char *name, const char *value) {
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

int nickserv_db_set_password(NickServDb *db, const char *name, const char *password_hash) {
    return set_text(db,
        "UPDATE nickserv_accounts SET password_hash=?1,updated_at=unixepoch() WHERE name=?2",
        name, password_hash);
}

int nickserv_db_set_vhost(NickServDb *db, const char *name, const char *vhost) {
    return set_text(db,
        "UPDATE nickserv_accounts SET vhost=?1,updated_at=unixepoch() WHERE name=?2",
        name, vhost);
}

int nickserv_db_set_enabled(NickServDb *db, const char *name, int enabled) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle,
            "UPDATE nickserv_accounts SET enabled=?1,updated_at=unixepoch() WHERE name=?2",
            -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}
