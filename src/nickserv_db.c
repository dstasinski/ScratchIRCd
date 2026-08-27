/**
 * @file nickserv_db.c
 * @brief SQLite persistence for registered NickServ accounts.
 *
 * Existing 0.13 databases are migrated in place by adding the recovery/email
 * columns when they are missing. Tokens are stored only as SHA-256 hex hashes;
 * plaintext verification/reset tokens never persist in SQLite.
 */

#include "nickserv_db.h"
#include "sqlite_policy.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define NICKSERV_DB_SCHEMA_VERSION 1

static const char *schema_sql =
    "CREATE TABLE IF NOT EXISTS nickserv_accounts ("
    "name TEXT COLLATE NOCASE PRIMARY KEY,"
    "password_hash TEXT NOT NULL,"
    "vhost TEXT NOT NULL DEFAULT '',"
    "enabled INTEGER NOT NULL DEFAULT 1,"
    "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
    "updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
    ");";

typedef struct NickServMigration {
    const char *column;
    const char *sql;
} NickServMigration;

static const NickServMigration migrations[] = {
    {"email", "ALTER TABLE nickserv_accounts ADD COLUMN email TEXT NOT NULL DEFAULT ''"},
    {"email_verified", "ALTER TABLE nickserv_accounts ADD COLUMN email_verified INTEGER NOT NULL DEFAULT 0"},
    {"pending_email", "ALTER TABLE nickserv_accounts ADD COLUMN pending_email TEXT NOT NULL DEFAULT ''"},
    {"email_verify_token_hash", "ALTER TABLE nickserv_accounts ADD COLUMN email_verify_token_hash TEXT NOT NULL DEFAULT ''"},
    {"email_verify_expires_at", "ALTER TABLE nickserv_accounts ADD COLUMN email_verify_expires_at INTEGER NOT NULL DEFAULT 0"},
    {"reset_token_hash", "ALTER TABLE nickserv_accounts ADD COLUMN reset_token_hash TEXT NOT NULL DEFAULT ''"},
    {"reset_expires_at", "ALTER TABLE nickserv_accounts ADD COLUMN reset_expires_at INTEGER NOT NULL DEFAULT 0"}
};

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

static int column_exists(sqlite3 *handle, const char *column) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    int found = 0;
    if (handle == NULL || column == NULL) return -1;
    if (sqlite3_prepare_v2(handle, "PRAGMA table_info(nickserv_accounts)",
                           -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        if (name != NULL && strcmp((const char *)name, column) == 0) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (found) return 1;
    return rc == SQLITE_DONE ? 0 : -1;
}

static int apply_migrations(sqlite3 *handle) {
    size_t i;
    for (i = 0U; i < sizeof(migrations) / sizeof(migrations[0]); ++i) {
        int exists = column_exists(handle, migrations[i].column);
        if (exists < 0) return -1;
        if (exists == 0 && sqlite3_exec(handle, migrations[i].sql,
                                        NULL, NULL, NULL) != SQLITE_OK)
            return -1;
    }
    return 0;
}

static int schema_version(sqlite3 *handle, int *version) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (handle == NULL || version == NULL) return -1;
    if (sqlite3_prepare_v2(handle, "PRAGMA user_version", -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) *version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 0 : -1;
}

static int migrate_schema(sqlite3 *handle) {
    int version = 0;
    if (schema_version(handle, &version) != 0) return -1;
    if (version >= NICKSERV_DB_SCHEMA_VERSION) return 0;
    if (apply_migrations(handle) != 0) return -1;
    return sqlite3_exec(handle, "PRAGMA user_version=1", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
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
    copy_text(account->email, sizeof(account->email), sqlite3_column_text(stmt, 6));
    account->email_verified = sqlite3_column_int(stmt, 7);
    copy_text(account->pending_email, sizeof(account->pending_email), sqlite3_column_text(stmt, 8));
    copy_text(account->email_verify_token_hash, sizeof(account->email_verify_token_hash), sqlite3_column_text(stmt, 9));
    account->email_verify_expires_at = sqlite3_column_int64(stmt, 10);
    copy_text(account->reset_token_hash, sizeof(account->reset_token_hash), sqlite3_column_text(stmt, 11));
    account->reset_expires_at = sqlite3_column_int64(stmt, 12);
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
    if (ircd_sqlite_apply_policy(db->handle) != 0) {
        nickserv_db_close(db);
        return -1;
    }
    if (sqlite3_exec(db->handle, schema_sql, NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        nickserv_db_close(db);
        return -1;
    }
    if (migrate_schema(db->handle) != 0) {
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
        "SELECT name,password_hash,vhost,enabled,created_at,updated_at,"
        "email,email_verified,pending_email,email_verify_token_hash,"
        "email_verify_expires_at,reset_token_hash,reset_expires_at "
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

static int account_count(NickServDb *db, size_t *count) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (count != NULL) *count = 0U;
    if (db == NULL || db->handle == NULL || count == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, "SELECT COUNT(*) FROM nickserv_accounts",
                           -1, &stmt, NULL) != SQLITE_OK) return -1;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) *count = (size_t)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 0 : -1;
}

int nickserv_db_add(NickServDb *db, const NickServAccount *account) {
    static const char sql[] =
        "INSERT INTO nickserv_accounts(name,password_hash,vhost,enabled,email,email_verified) "
        "VALUES(?1,?2,?3,?4,?5,?6)";
    sqlite3_stmt *stmt = NULL;
    size_t count = 0U;
    int rc;

    if (db == NULL || db->handle == NULL || account == NULL) return -1;
    if (account_count(db, &count) != 0 || count >= IRCD_NICKSERV_ACCOUNT_HARD_MAX) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, account->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, account->password_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, account->vhost, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, account->enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 5, account->email, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, account->email_verified ? 1 : 0);
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
        "UPDATE nickserv_accounts SET password_hash=?1,reset_token_hash='',reset_expires_at=0,updated_at=unixepoch() WHERE name=?2",
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

int nickserv_db_set_email_challenge(NickServDb *db, const char *name,
                                    const char *pending_email,
                                    const char *token_hash,
                                    long long expires_at) {
    static const char sql[] =
        "UPDATE nickserv_accounts SET pending_email=?1,email_verify_token_hash=?2,"
        "email_verify_expires_at=?3,updated_at=unixepoch() WHERE name=?4";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL || pending_email == NULL || token_hash == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, pending_email, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, token_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, expires_at);
    sqlite3_bind_text(stmt, 4, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

int nickserv_db_verify_email(NickServDb *db, const char *name,
                             const char *token_hash, long long now) {
    static const char sql[] =
        "UPDATE nickserv_accounts SET email=pending_email,email_verified=1,"
        "pending_email='',email_verify_token_hash='',email_verify_expires_at=0,"
        "updated_at=unixepoch() WHERE name=?1 AND email_verify_token_hash=?2 "
        "AND email_verify_expires_at>=?3 AND pending_email<>''";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL || token_hash == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, token_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? (sqlite3_changes(db->handle) > 0 ? 1 : 0) : -1;
}

int nickserv_db_admin_set_email(NickServDb *db, const char *name,
                                const char *email, int verified) {
    static const char sql[] =
        "UPDATE nickserv_accounts SET email=?1,email_verified=?2,pending_email='',"
        "email_verify_token_hash='',email_verify_expires_at=0,updated_at=unixepoch() WHERE name=?3";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL || email == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, email, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, verified ? 1 : 0);
    sqlite3_bind_text(stmt, 3, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

int nickserv_db_set_reset_token(NickServDb *db, const char *name,
                                const char *token_hash, long long expires_at) {
    static const char sql[] =
        "UPDATE nickserv_accounts SET reset_token_hash=?1,reset_expires_at=?2,"
        "updated_at=unixepoch() WHERE name=?3";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL || token_hash == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, token_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, expires_at);
    sqlite3_bind_text(stmt, 3, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

int nickserv_db_consume_reset_token(NickServDb *db, const char *name,
                                    const char *token_hash, long long now,
                                    const char *new_password_hash) {
    static const char sql[] =
        "UPDATE nickserv_accounts SET password_hash=?1,reset_token_hash='',"
        "reset_expires_at=0,updated_at=unixepoch() WHERE name=?2 "
        "AND reset_token_hash=?3 AND reset_expires_at>=?4 AND enabled=1";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || name == NULL || token_hash == NULL || new_password_hash == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, new_password_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, token_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, now);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? (sqlite3_changes(db->handle) > 0 ? 1 : 0) : -1;
}