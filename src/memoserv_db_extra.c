/**
 * @file memoserv_db_extra.c
 * @brief Extended MemoServ queries for SENT, quota accounting, and retention.
 */

#include "memoserv_db.h"

#include <stdio.h>
#include <string.h>

static void fill_memo(sqlite3_stmt *stmt, MemoServMemo *memo) {
    const char *sender = (const char *)sqlite3_column_text(stmt, 1);
    const char *recipient = (const char *)sqlite3_column_text(stmt, 2);
    const char *text = (const char *)sqlite3_column_text(stmt, 3);
    memset(memo, 0, sizeof(*memo));
    memo->id = (long long)sqlite3_column_int64(stmt, 0);
    (void)snprintf(memo->sender, sizeof(memo->sender), "%s", sender != NULL ? sender : "");
    (void)snprintf(memo->recipient, sizeof(memo->recipient), "%s", recipient != NULL ? recipient : "");
    (void)snprintf(memo->text, sizeof(memo->text), "%s", text != NULL ? text : "");
    memo->created_at = (long long)sqlite3_column_int64(stmt, 4);
    memo->read_at = (long long)sqlite3_column_int64(stmt, 5);
}

int memoserv_db_count(MemoServDb *db, const char *recipient, size_t *count) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || recipient == NULL || count == NULL) return -1;
    *count = 0U;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT COUNT(*) FROM memos WHERE recipient=?1",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, recipient, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) *count = (size_t)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 0 : -1;
}

int memoserv_db_list_sent(MemoServDb *db, const char *sender,
                          MemoServMemo *memos, size_t capacity, size_t *count) {
    sqlite3_stmt *stmt = NULL;
    size_t used = 0U;
    int rc = SQLITE_DONE;
    if (db == NULL || db->handle == NULL || sender == NULL || memos == NULL ||
        capacity == 0U || count == NULL) return -1;
    *count = 0U;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT id,sender,recipient,text,created_at,read_at FROM memos "
        "WHERE sender=?1 ORDER BY id DESC LIMIT ?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)capacity);
    while (used < capacity && (rc = sqlite3_step(stmt)) == SQLITE_ROW)
        fill_memo(stmt, &memos[used++]);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    *count = used;
    return 0;
}

int memoserv_db_get_sent(MemoServDb *db, const char *sender,
                         long long memo_id, MemoServMemo *memo) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || sender == NULL || memo == NULL || memo_id <= 0) return -1;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT id,sender,recipient,text,created_at,read_at FROM memos "
        "WHERE sender=?1 AND id=?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, memo_id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) fill_memo(stmt, memo);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 1 : rc == SQLITE_DONE ? 0 : -1;
}

int memoserv_db_purge_before(MemoServDb *db, const char *recipient,
                             long long cutoff, size_t *deleted) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || cutoff <= 0) return -1;
    if (recipient != NULL) {
        if (sqlite3_prepare_v2(db->handle,
            "DELETE FROM memos WHERE recipient=?1 AND created_at<?2",
            -1, &stmt, NULL) != SQLITE_OK) return -1;
        sqlite3_bind_text(stmt, 1, recipient, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, cutoff);
    } else {
        if (sqlite3_prepare_v2(db->handle,
            "DELETE FROM memos WHERE created_at<?1",
            -1, &stmt, NULL) != SQLITE_OK) return -1;
        sqlite3_bind_int64(stmt, 1, cutoff);
    }
    rc = sqlite3_step(stmt);
    if (deleted != NULL) *deleted = rc == SQLITE_DONE ? (size_t)sqlite3_changes(db->handle) : 0U;
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}
