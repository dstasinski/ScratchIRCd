/**
 * @file memoserv_db_extra.c
 * @brief Extended MemoServ queries for SENT, quota accounting, and retention.
 */

#include "memoserv_db.h"

#include <string.h>

static int account_arg_fits(const char *account) {
    size_t length;
    if (account == NULL) return 0;
    length = strlen(account);
    return length != 0U && length <= IRC_NICK_MAX &&
           memchr(account, '\r', length) == NULL &&
           memchr(account, '\n', length) == NULL;
}

static int copy_text_column(sqlite3_stmt *stmt, int column,
                            char *destination, size_t destination_size) {
    const unsigned char *text;
    int bytes;
    if (stmt == NULL || destination == NULL || destination_size == 0U) return -1;
    text = sqlite3_column_text(stmt, column);
    bytes = sqlite3_column_bytes(stmt, column);
    if (text == NULL || bytes < 0 || (size_t)bytes >= destination_size ||
        memchr(text, '\0', (size_t)bytes) != NULL ||
        memchr(text, '\r', (size_t)bytes) != NULL ||
        memchr(text, '\n', (size_t)bytes) != NULL)
        return -1;
    memcpy(destination, text, (size_t)bytes);
    destination[bytes] = '\0';
    return 0;
}

static int fill_memo(sqlite3_stmt *stmt, MemoServMemo *memo) {
    if (stmt == NULL || memo == NULL) return -1;
    memset(memo, 0, sizeof(*memo));
    if (copy_text_column(stmt, 1, memo->sender, sizeof(memo->sender)) != 0 ||
        copy_text_column(stmt, 2, memo->recipient, sizeof(memo->recipient)) != 0 ||
        copy_text_column(stmt, 3, memo->text, sizeof(memo->text)) != 0) {
        memset(memo, 0, sizeof(*memo));
        return -1;
    }
    memo->id = (long long)sqlite3_column_int64(stmt, 0);
    memo->created_at = (long long)sqlite3_column_int64(stmt, 4);
    memo->read_at = (long long)sqlite3_column_int64(stmt, 5);
    return 0;
}

int memoserv_db_count(MemoServDb *db, const char *recipient, size_t *count) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || !account_arg_fits(recipient) || count == NULL) return -1;
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
    if (db == NULL || db->handle == NULL || !account_arg_fits(sender) || memos == NULL ||
        capacity == 0U || count == NULL) return -1;
    *count = 0U;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT id,sender,recipient,text,created_at,read_at FROM memos "
        "WHERE sender=?1 ORDER BY id DESC LIMIT ?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)capacity);
    while (used < capacity && (rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (fill_memo(stmt, &memos[used]) != 0) {
            sqlite3_finalize(stmt);
            return -1;
        }
        ++used;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    *count = used;
    return 0;
}

int memoserv_db_get_sent(MemoServDb *db, const char *sender,
                         long long memo_id, MemoServMemo *memo) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || !account_arg_fits(sender) ||
        memo == NULL || memo_id <= 0) return -1;
    if (sqlite3_prepare_v2(db->handle,
        "SELECT id,sender,recipient,text,created_at,read_at FROM memos "
        "WHERE sender=?1 AND id=?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, memo_id);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW && fill_memo(stmt, memo) != 0) {
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 1 : rc == SQLITE_DONE ? 0 : -1;
}

int memoserv_db_purge_before(MemoServDb *db, const char *recipient,
                             long long cutoff, size_t *deleted) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || cutoff <= 0 ||
        (recipient != NULL && !account_arg_fits(recipient))) return -1;
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