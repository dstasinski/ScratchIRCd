/**
 * @file memoserv_db.c
 * @brief SQLite persistence for MemoServ account-to-account messages.
 */

#include "memoserv_db.h"

#include <stdio.h>
#include <string.h>

static int exec_sql(sqlite3 *db, const char *sql) {
    char *error = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &error);
    if (rc != SQLITE_OK) {
        if (error != NULL) fprintf(stderr, "MemoServ database: %s\n", error);
        sqlite3_free(error);
        return -1;
    }
    return 0;
}

int memoserv_db_open(MemoServDb *db, const char *path) {
    static const char schema[] =
        "CREATE TABLE IF NOT EXISTS memos ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender TEXT COLLATE NOCASE NOT NULL,"
        "recipient TEXT COLLATE NOCASE NOT NULL,"
        "text TEXT NOT NULL,"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "read_at INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX IF NOT EXISTS memos_recipient_id "
        "ON memos(recipient,id DESC);"
        "CREATE INDEX IF NOT EXISTS memos_recipient_unread "
        "ON memos(recipient,read_at);";

    if (db == NULL || path == NULL || *path == '\0') return -1;
    db->handle = NULL;
    if (sqlite3_open(path, &db->handle) != SQLITE_OK) {
        if (db->handle != NULL) sqlite3_close(db->handle);
        db->handle = NULL;
        return -1;
    }
    sqlite3_busy_timeout(db->handle, 2000);
    if (exec_sql(db->handle, schema) != 0) {
        memoserv_db_close(db);
        return -1;
    }
    return 0;
}

void memoserv_db_close(MemoServDb *db) {
    if (db == NULL || db->handle == NULL) return;
    sqlite3_close(db->handle);
    db->handle = NULL;
}

int memoserv_db_send(MemoServDb *db, const char *sender,
                     const char *recipient, const char *text,
                     long long *memo_id) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || sender == NULL || recipient == NULL ||
        text == NULL || *sender == '\0' || *recipient == '\0' || *text == '\0' ||
        strlen(text) > IRCD_MEMOSERV_TEXT_MAX) return -1;
    if (sqlite3_prepare_v2(db->handle,
        "INSERT INTO memos(sender,recipient,text) VALUES(?1,?2,?3)",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, recipient, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, text, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    if (memo_id != NULL) *memo_id = (long long)sqlite3_last_insert_rowid(db->handle);
    return 0;
}

int memoserv_db_unread_count(MemoServDb *db, const char *recipient,
                             size_t *count) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || recipient == NULL || count == NULL) return -1;
    *count = 0U;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT COUNT(*) FROM memos WHERE recipient=?1 AND read_at=0",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, recipient, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) *count = (size_t)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 0 : -1;
}

int memoserv_db_list(MemoServDb *db, const char *recipient,
                     MemoServMemo *memos, size_t capacity, size_t *count) {
    sqlite3_stmt *stmt = NULL;
    size_t used = 0U;
    int rc;
    if (db == NULL || db->handle == NULL || recipient == NULL ||
        memos == NULL || capacity == 0U || count == NULL) return -1;
    *count = 0U;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT id,sender,recipient,text,created_at,read_at FROM memos "
        "WHERE recipient=?1 ORDER BY id DESC LIMIT ?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, recipient, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)capacity);
    while (used < capacity && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *sender = (const char *)sqlite3_column_text(stmt, 1);
        const char *recip = (const char *)sqlite3_column_text(stmt, 2);
        const char *text = (const char *)sqlite3_column_text(stmt, 3);
        MemoServMemo *memo = &memos[used++];
        memset(memo, 0, sizeof(*memo));
        memo->id = (long long)sqlite3_column_int64(stmt, 0);
        (void)snprintf(memo->sender, sizeof(memo->sender), "%s", sender != NULL ? sender : "");
        (void)snprintf(memo->recipient, sizeof(memo->recipient), "%s", recip != NULL ? recip : "");
        (void)snprintf(memo->text, sizeof(memo->text), "%s", text != NULL ? text : "");
        memo->created_at = (long long)sqlite3_column_int64(stmt, 4);
        memo->read_at = (long long)sqlite3_column_int64(stmt, 5);
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    *count = used;
    return 0;
}

int memoserv_db_get(MemoServDb *db, const char *recipient,
                    long long memo_id, MemoServMemo *memo) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || recipient == NULL || memo == NULL || memo_id <= 0) return -1;
    memset(memo, 0, sizeof(*memo));
    if (sqlite3_prepare_v2(db->handle,
        "SELECT id,sender,recipient,text,created_at,read_at FROM memos "
        "WHERE recipient=?1 AND id=?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, recipient, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)memo_id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char *sender = (const char *)sqlite3_column_text(stmt, 1);
        const char *recip = (const char *)sqlite3_column_text(stmt, 2);
        const char *text = (const char *)sqlite3_column_text(stmt, 3);
        memo->id = (long long)sqlite3_column_int64(stmt, 0);
        (void)snprintf(memo->sender, sizeof(memo->sender), "%s", sender != NULL ? sender : "");
        (void)snprintf(memo->recipient, sizeof(memo->recipient), "%s", recip != NULL ? recip : "");
        (void)snprintf(memo->text, sizeof(memo->text), "%s", text != NULL ? text : "");
        memo->created_at = (long long)sqlite3_column_int64(stmt, 4);
        memo->read_at = (long long)sqlite3_column_int64(stmt, 5);
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 1 : rc == SQLITE_DONE ? 0 : -1;
}

int memoserv_db_mark_read(MemoServDb *db, const char *recipient,
                          long long memo_id, long long when) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || recipient == NULL || memo_id <= 0 || when <= 0) return -1;
    if (sqlite3_prepare_v2(db->handle,
        "UPDATE memos SET read_at=CASE WHEN read_at=0 THEN ?3 ELSE read_at END "
        "WHERE recipient=?1 AND id=?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, recipient, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)memo_id);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)when);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int memoserv_db_delete(MemoServDb *db, const char *recipient,
                       long long memo_id) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    int changed;
    if (db == NULL || db->handle == NULL || recipient == NULL || memo_id <= 0) return -1;
    if (sqlite3_prepare_v2(db->handle,
        "DELETE FROM memos WHERE recipient=?1 AND id=?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, recipient, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)memo_id);
    rc = sqlite3_step(stmt);
    changed = sqlite3_changes(db->handle);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? (changed > 0 ? 1 : 0) : -1;
}

int memoserv_db_delete_all(MemoServDb *db, const char *recipient) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || recipient == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle,
        "DELETE FROM memos WHERE recipient=?1",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, recipient, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}
