/**
 * @file history_db.c
 * @brief SQLite persistence for IRCv3 channel message history.
 *
 * History is intentionally stored independently of live Channel objects so a
 * future persistent ChanServ channel can restore policy/state without changing
 * the storage format. This first iteration stores accepted channel PRIVMSG and
 * NOTICE events only.
 */

#include "history_db.h"

#include <errno.h>
#include <stdio.h>
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
    "ON history(target, created_at_ms, id);";

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

int history_db_add(HistoryDb *db, const HistoryRecord *record) {
    static const char *sql =
        "INSERT INTO history(target,command,nick,user,host,account,text,created_at_ms) "
        "VALUES(?,?,?,?,?,?,?,?)";
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (db == NULL || db->handle == NULL || record == NULL) return -1;
    if (strcmp(record->command, "PRIVMSG") != 0 &&
        strcmp(record->command, "NOTICE") != 0) return -1;
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
