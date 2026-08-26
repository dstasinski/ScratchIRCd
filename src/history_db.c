/**
 * @file history_db.c
 * @brief SQLite persistence for IRCv3 channel message history.
 */

#include "history_db.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *schema_sql =
    "CREATE TABLE IF NOT EXISTS history ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "target TEXT COLLATE NOCASE NOT NULL,"
    "command TEXT NOT NULL,"
    "nick TEXT NOT NULL,"
    "user TEXT NOT NULL,"
    "host TEXT NOT NULL,"
    "account TEXT NOT NULL DEFAULT '',"
    "text TEXT NOT NULL,"
    "created_at_ms INTEGER NOT NULL"
    ");"
    "CREATE INDEX IF NOT EXISTS history_target_time "
    "ON history(target, created_at_ms, id);"
    "CREATE INDEX IF NOT EXISTS history_created_time "
    "ON history(created_at_ms, id);";

static HistoryDb shared_db = {0};
static char shared_path[IRCD_CONFIG_PATH_MAX + 1U];
static int shared_cleanup_registered = 0;
static int64_t shared_last_maintenance_ms = 0;
static unsigned int shared_last_retention_days = 0U;
static size_t shared_last_max_rows = 0U;

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

int history_db_open(HistoryDb *db, const char *path) {
    char *error = NULL;
    if (db == NULL || path == NULL || *path == '\0') return -1;
    memset(db, 0, sizeof(*db));
    if (ensure_parent_directory(path) != 0) return -1;
    if (sqlite3_open(path, &db->handle) != SQLITE_OK) {
        history_db_close(db);
        return -1;
    }
    (void)sqlite3_busy_timeout(db->handle, 1000);
    if (sqlite3_exec(db->handle, schema_sql, NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        history_db_close(db);
        return -1;
    }
    return 0;
}

void history_db_close(HistoryDb *db) {
    if (db == NULL) return;
    if (db->handle != NULL) sqlite3_close(db->handle);
    db->handle = NULL;
}

static void shared_history_cleanup(void) {
    history_db_close(&shared_db);
    shared_path[0] = '\0';
    shared_last_maintenance_ms = 0;
    shared_last_retention_days = 0U;
    shared_last_max_rows = 0U;
}

HistoryDb *history_db_shared(const char *path) {
    if (path == NULL || *path == '\0' || strlen(path) > IRCD_CONFIG_PATH_MAX) return NULL;
    if (shared_db.handle != NULL && strcmp(shared_path, path) == 0) return &shared_db;
    if (shared_db.handle != NULL) shared_history_cleanup();
    if (history_db_open(&shared_db, path) != 0) {
        shared_path[0] = '\0';
        return NULL;
    }
    (void)snprintf(shared_path, sizeof(shared_path), "%s", path);
    if (!shared_cleanup_registered && atexit(shared_history_cleanup) == 0)
        shared_cleanup_registered = 1;
    return &shared_db;
}

int history_db_add(HistoryDb *db, const HistoryRecord *record) {
    static const char *sql =
        "INSERT INTO history(target,command,nick,user,host,account,text,created_at_ms) "
        "VALUES(?,?,?,?,?,?,?,?)";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || record == NULL) return -1;
    if (strcmp(record->command, "PRIVMSG") != 0 && strcmp(record->command, "NOTICE") != 0) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, record->target, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record->command, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, record->nick, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, record->user, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, record->host, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, record->account, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, record->text, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 8, record->created_at_ms);
    rc = sqlite3_step(stmt) == SQLITE_DONE ? 0 : -1;
    sqlite3_finalize(stmt);
    return rc;
}

int history_db_prune(HistoryDb *db, unsigned int retention_days,
                     size_t max_rows, int64_t now_ms) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || max_rows == 0U || now_ms < 0) return -1;

    if (retention_days != 0U) {
        const int64_t cutoff = now_ms - (int64_t)retention_days * 86400000LL;
        if (sqlite3_prepare_v2(db->handle,
                "DELETE FROM history WHERE created_at_ms < ?1", -1, &stmt, NULL) != SQLITE_OK)
            return -1;
        sqlite3_bind_int64(stmt, 1, cutoff);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        stmt = NULL;
        if (rc != SQLITE_DONE) return -1;
    }

    if (sqlite3_prepare_v2(db->handle,
            "DELETE FROM history WHERE id <= ("
            "SELECT id FROM history ORDER BY id DESC LIMIT 1 OFFSET ?1)",
            -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)max_rows);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int history_db_shared_maintain(const char *path, unsigned int retention_days,
                               size_t max_rows, int64_t now_ms) {
    HistoryDb *db;
    int policy_changed;
    if (path == NULL || max_rows == 0U || now_ms < 0) return -1;
    db = history_db_shared(path);
    if (db == NULL) return -1;
    policy_changed = retention_days != shared_last_retention_days ||
                     max_rows != shared_last_max_rows;
    if (!policy_changed && shared_last_maintenance_ms != 0 &&
        now_ms >= shared_last_maintenance_ms &&
        now_ms - shared_last_maintenance_ms < 300000LL)
        return 0;
    if (history_db_prune(db, retention_days, max_rows, now_ms) != 0) return -1;
    shared_last_maintenance_ms = now_ms;
    shared_last_retention_days = retention_days;
    shared_last_max_rows = max_rows;
    return 0;
}

static void copy_column(char *dest, size_t size, sqlite3_stmt *stmt, int column) {
    const unsigned char *value = sqlite3_column_text(stmt, column);
    (void)snprintf(dest, size, "%s", value != NULL ? (const char *)value : "");
}

int history_db_latest(HistoryDb *db, const char *target, size_t limit,
                      HistoryRecord *records, size_t capacity, size_t *count) {
    static const char *sql =
        "SELECT id,target,command,nick,user,host,account,text,created_at_ms "
        "FROM (SELECT id,target,command,nick,user,host,account,text,created_at_ms "
        "FROM history WHERE target=? ORDER BY created_at_ms DESC,id DESC LIMIT ?) "
        "ORDER BY created_at_ms ASC,id ASC";
    sqlite3_stmt *stmt = NULL;
    size_t used = 0U;
    int step;
    if (count != NULL) *count = 0U;
    if (db == NULL || db->handle == NULL || target == NULL || records == NULL ||
        capacity == 0U || limit == 0U) return -1;
    if (limit > capacity) limit = capacity;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, target, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)limit);
    while ((step = sqlite3_step(stmt)) == SQLITE_ROW && used < capacity) {
        HistoryRecord *record = &records[used++];
        memset(record, 0, sizeof(*record));
        record->id = sqlite3_column_int64(stmt, 0);
        copy_column(record->target, sizeof(record->target), stmt, 1);
        copy_column(record->command, sizeof(record->command), stmt, 2);
        copy_column(record->nick, sizeof(record->nick), stmt, 3);
        copy_column(record->user, sizeof(record->user), stmt, 4);
        copy_column(record->host, sizeof(record->host), stmt, 5);
        copy_column(record->account, sizeof(record->account), stmt, 6);
        copy_column(record->text, sizeof(record->text), stmt, 7);
        record->created_at_ms = sqlite3_column_int64(stmt, 8);
    }
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE) return -1;
    if (count != NULL) *count = used;
    return 0;
}
