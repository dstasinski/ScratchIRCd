/** @file test_memoserv_db.c @brief Unit tests for MemoServ SQLite persistence. */
#include "memoserv_db.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fill_overlong(char *buffer, size_t valid_max, char ch) {
    memset(buffer, ch, valid_max + 1U);
    buffer[valid_max + 1U] = '\0';
}

static void raw_set_memo_text(sqlite3 *db, const char *column,
                              long long id, const char *value) {
    sqlite3_stmt *stmt = NULL;
    char sql[128];
    int written = snprintf(sql, sizeof(sql),
                           "UPDATE memos SET %s=?1 WHERE id=?2", column);
    assert(written > 0 && (size_t)written < sizeof(sql));
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, id);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_changes(db) == 1);
    sqlite3_finalize(stmt);
}

static int raw_index_exists(sqlite3 *db, const char *name) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    int found = 0;
    assert(sqlite3_prepare_v2(db,
        "SELECT 1 FROM sqlite_master WHERE type='index' AND name=?1",
        -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    found = rc == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

static int raw_column_exists(sqlite3 *db, const char *column_name) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    int found = 0;
    assert(sqlite3_prepare_v2(db, "PRAGMA table_info(memos)", -1,
                              &stmt, NULL) == SQLITE_OK);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        if (name != NULL && strcmp((const char *)name, column_name) == 0) {
            found = 1;
            break;
        }
    }
    assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return found;
}

static int raw_memo_row_exists(sqlite3 *db, long long id) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    int found = 0;
    assert(sqlite3_prepare_v2(db, "SELECT 1 FROM memos WHERE id=?1",
                              -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)id);
    rc = sqlite3_step(stmt);
    found = rc == SQLITE_ROW;
    assert(rc == SQLITE_ROW || rc == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return found;
}

static void create_incompatible_memoserv_db(const char *path) {
    sqlite3 *legacy = NULL;
    char *error = NULL;
    static const char sql[] =
        "CREATE TABLE memos ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender TEXT COLLATE NOCASE NOT NULL,"
        "recipient TEXT COLLATE NOCASE NOT NULL,"
        "text TEXT NOT NULL,"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch())"
        ");"
        "INSERT INTO memos(sender,recipient,text,created_at) "
        "VALUES('Alice','Bob','broken memo',12345);";

    assert(sqlite3_open(path, &legacy) == SQLITE_OK);
    assert(sqlite3_exec(legacy, sql, NULL, NULL, &error) == SQLITE_OK);
    assert(error == NULL);
    assert(!raw_column_exists(legacy, "read_at"));
    sqlite3_close(legacy);
}

static void assert_incompatible_schema_rejected(const char *path) {
    char capture_path[] = "/tmp/scratchircd-memoserv-stderr-XXXXXX";
    char buffer[512];
    MemoServDb db = {0};
    int capture_fd = mkstemp(capture_path);
    int saved_stderr;
    ssize_t got;

    assert(capture_fd >= 0);
    saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0);
    fflush(stderr);
    assert(dup2(capture_fd, STDERR_FILENO) >= 0);

    assert(memoserv_db_open(&db, path) == -1);
    assert(db.handle == NULL);

    fflush(stderr);
    assert(dup2(saved_stderr, STDERR_FILENO) >= 0);
    close(saved_stderr);
    assert(lseek(capture_fd, 0, SEEK_SET) == 0);
    got = read(capture_fd, buffer, sizeof(buffer) - 1U);
    assert(got >= 0);
    buffer[got] = '\0';
    close(capture_fd);
    unlink(capture_path);

    assert(strstr(buffer,
                  "MemoServ database: incompatible memos schema missing read_at column") != NULL);
}

static void assert_current_indexes(sqlite3 *db) {
    assert(raw_index_exists(db, "memos_sender_visible_id"));
    assert(raw_index_exists(db, "memos_recipient_visible_id"));
    assert(raw_index_exists(db, "memos_recipient_unread"));
    assert(raw_index_exists(db, "memos_sender_outstanding_created"));
}

int main(void) {
    char path[] = "/tmp/scratchircd-memoserv-XXXXXX";
    char incompatible_path[] = "/tmp/scratchircd-memoserv-broken-XXXXXX";
    int fd = mkstemp(path);
    int incompatible_fd = mkstemp(incompatible_path);
    MemoServDb db = {0};
    MemoServMemo memos[8];
    MemoServMemo memo;
    char long_account[IRC_NICK_MAX + 2U];
    char long_text[IRCD_MEMOSERV_TEXT_MAX + 2U];
    size_t count = 0U;
    size_t unread = 0U;
    size_t outstanding = 0U;
    size_t deleted = 0U;
    long long first = 0;
    long long second = 0;
    long long third = 0;
    long long fourth = 0;

    fill_overlong(long_account, IRC_NICK_MAX, 'A');
    fill_overlong(long_text, IRCD_MEMOSERV_TEXT_MAX, 'M');

    assert(fd >= 0);
    assert(incompatible_fd >= 0);
    close(fd);
    close(incompatible_fd);
    unlink(path);
    unlink(incompatible_path);

    create_incompatible_memoserv_db(incompatible_path);
    assert_incompatible_schema_rejected(incompatible_path);
    unlink(incompatible_path);

    assert(memoserv_db_open(&db, path) == 0);
    assert_current_indexes(db.handle);
    assert(memoserv_db_count_sender_outstanding(&db, "Alice", 0, &outstanding) == 0);
    assert(outstanding == 0U);

    /* Public writes must reject data that cannot round-trip through the fixed
     * MemoServ record structure. */
    assert(memoserv_db_send(&db, long_account, "Bob", "bad sender", NULL) == -1);
    assert(memoserv_db_send(&db, "Alice", long_account, "bad recipient", NULL) == -1);
    assert(memoserv_db_send(&db, "Alice", "Bob", long_text, NULL) == -1);
    assert(memoserv_db_send(&db, "Alice\nMallory", "Bob", "bad sender", NULL) == -1);
    assert(memoserv_db_send(&db, "Alice", "Bob\rMallory", "bad recipient", NULL) == -1);
    assert(memoserv_db_send(&db, "Alice", "Bob", "bad\nbody", NULL) == -1);

    assert(memoserv_db_send(&db, "Alice", "Bob", "first memo", &first) == 0);
    assert(memoserv_db_send(&db, "Carol", "Bob", "second memo", &second) == 0);
    assert(memoserv_db_send(&db, "Alice", "Dave", "sent memo", &third) == 0);
    assert(first > 0 && second > first && third > second);
    assert(memoserv_db_count_sender_outstanding(&db, "alice", 0, &outstanding) == 0);
    assert(outstanding == 2U);
    assert(memoserv_db_count_sender_outstanding(&db, "Alice", 1, &outstanding) == 0);
    assert(outstanding == 2U);
    assert(memoserv_db_count_sender_outstanding(&db, "Alice", 4102444800LL, &outstanding) == 0);
    assert(outstanding == 0U);

    assert(memoserv_db_count(&db, "bob", &count) == 0);
    assert(count == 2U);
    assert(memoserv_db_unread_count(&db, "bob", &unread) == 0);
    assert(unread == 2U);

    assert(memoserv_db_list(&db, "BOB", memos, 8U, &count) == 0);
    assert(count == 2U);
    assert(memos[0].id == second);
    assert(strcmp(memos[0].sender, "Carol") == 0);
    assert(memos[1].id == first);

    assert(memoserv_db_list_sent(&db, "alice", memos, 8U, &count) == 0);
    assert(count == 2U);
    assert(memos[0].id == third);
    assert(strcmp(memos[0].recipient, "Dave") == 0);
    assert(memoserv_db_get_sent(&db, "ALICE", first, &memo) == 1);
    assert(strcmp(memo.recipient, "Bob") == 0);

    assert(memoserv_db_get(&db, "Bob", first, &memo) == 1);
    assert(strcmp(memo.text, "first memo") == 0);
    assert(memo.read_at == 0);
    assert(memoserv_db_get(&db, "Alice", first, &memo) == 0);

    /* Inbox deletion is recipient-owned visibility only. The sender's sent
     * history remains visible unless the sender uses DELSENT. */
    assert(memoserv_db_delete(&db, "Bob", first) == 1);
    assert(memoserv_db_delete(&db, "Bob", first) == 0);
    assert(memoserv_db_get(&db, "Bob", first, &memo) == 0);
    assert(raw_memo_row_exists(db.handle, first));
    assert(memoserv_db_count(&db, "Bob", &count) == 0);
    assert(count == 1U);
    assert(memoserv_db_unread_count(&db, "Bob", &unread) == 0);
    assert(unread == 1U);
    assert(memoserv_db_count_sender_outstanding(&db, "Alice", 0, &outstanding) == 0);
    assert(outstanding == 1U);
    assert(memoserv_db_get_sent(&db, "Alice", first, &memo) == 1);
    assert(strcmp(memo.text, "first memo") == 0);

    /* Sent-history deletion is sender-owned visibility only until the
     * recipient also deletes the memo. */
    assert(memoserv_db_delete_sent(&db, "Mallory", third) == 0);
    assert(memoserv_db_delete_sent(&db, "ALICE", third) == 1);
    assert(raw_memo_row_exists(db.handle, third));
    assert(memoserv_db_count_sender_outstanding(&db, "Alice", 0, &outstanding) == 0);
    assert(outstanding == 1U);
    assert(memoserv_db_get_sent(&db, "Alice", third, &memo) == 0);
    assert(memoserv_db_count(&db, "Dave", &count) == 0);
    assert(count == 1U);
    assert(memoserv_db_get(&db, "Dave", third, &memo) == 1);
    assert(strcmp(memo.text, "sent memo") == 0);
    assert(memoserv_db_delete(&db, "Dave", third) == 1);
    assert(memoserv_db_get(&db, "Dave", third, &memo) == 0);
    assert(!raw_memo_row_exists(db.handle, third));
    assert(memoserv_db_count_sender_outstanding(&db, "Alice", 0, &outstanding) == 0);
    assert(outstanding == 0U);
    assert(memoserv_db_count(&db, "Bob", &count) == 0);
    assert(count == 1U);

    assert(memoserv_db_send(&db, "Alice", "Erin", "another sent memo", &fourth) == 0);
    assert(fourth > third);
    assert(memoserv_db_count_sender_outstanding(&db, "Alice", 0, &outstanding) == 0);
    assert(outstanding == 1U);
    assert(memoserv_db_delete_all_sent(&db, "ALICE") == 0);
    assert(memoserv_db_count_sender_outstanding(&db, "Alice", 0, &outstanding) == 0);
    assert(outstanding == 1U);
    assert(memoserv_db_list_sent(&db, "Alice", memos, 8U, &count) == 0);
    assert(count == 0U);
    assert(memoserv_db_count(&db, "Bob", &count) == 0);
    assert(count == 1U);
    assert(memoserv_db_get(&db, "Bob", first, &memo) == 0);
    assert(!raw_memo_row_exists(db.handle, first));
    assert(memoserv_db_get(&db, "Bob", second, &memo) == 1);
    assert(memoserv_db_count(&db, "Erin", &count) == 0);
    assert(count == 1U);

    /* Recreate an Alice-owned visible sent row for sent corruption and
     * account-purge checks. */
    assert(memoserv_db_send(&db, "Alice", "Dave", "sent memo", &third) == 0);

    /* External corruption must fail closed rather than returning a clipped or
     * multi-line sender, recipient, or memo body. */
    raw_set_memo_text(db.handle, "text", second, long_text);
    assert(memoserv_db_get(&db, "Bob", second, &memo) == -1);
    assert(memoserv_db_list(&db, "Bob", memos, 8U, &count) == -1);
    assert(memoserv_db_list_sent(&db, "Carol", memos, 8U, &count) == -1);
    raw_set_memo_text(db.handle, "text", second, "second memo");
    assert(memoserv_db_get(&db, "Bob", second, &memo) == 1);

    raw_set_memo_text(db.handle, "text", second, "second\nmemo");
    assert(memoserv_db_get(&db, "Bob", second, &memo) == -1);
    assert(memoserv_db_list(&db, "Bob", memos, 8U, &count) == -1);
    raw_set_memo_text(db.handle, "text", second, "second memo");
    assert(memoserv_db_get(&db, "Bob", second, &memo) == 1);

    raw_set_memo_text(db.handle, "sender", second, long_account);
    assert(memoserv_db_list(&db, "Bob", memos, 8U, &count) == -1);
    raw_set_memo_text(db.handle, "sender", second, "Carol");
    assert(memoserv_db_list(&db, "Bob", memos, 8U, &count) == 0);

    raw_set_memo_text(db.handle, "sender", second, "Car\rol");
    assert(memoserv_db_list(&db, "Bob", memos, 8U, &count) == -1);
    raw_set_memo_text(db.handle, "sender", second, "Carol");
    assert(memoserv_db_list(&db, "Bob", memos, 8U, &count) == 0);

    raw_set_memo_text(db.handle, "recipient", third, long_account);
    assert(memoserv_db_get_sent(&db, "Alice", third, &memo) == -1);
    assert(memoserv_db_list_sent(&db, "Alice", memos, 8U, &count) == -1);
    raw_set_memo_text(db.handle, "recipient", third, "Dave");
    assert(memoserv_db_get_sent(&db, "Alice", third, &memo) == 1);

    assert(memoserv_db_mark_read(&db, "Bob", second, 12345) == 0);
    assert(memoserv_db_unread_count(&db, "Bob", &unread) == 0);
    assert(unread == 0U);
    assert(memoserv_db_get(&db, "Bob", second, &memo) == 1);
    assert(memo.read_at == 12345);

    /* Dropped account cleanup physically removes both inbox and sent rows,
     * including sender-hidden and recipient-hidden rows. */
    assert(memoserv_db_delete_account(&db, "ALICE", &deleted) == 0);
    assert(deleted == 2U);
    assert(memoserv_db_list_sent(&db, "Alice", memos, 8U, &count) == 0);
    assert(count == 0U);
    assert(memoserv_db_count_sender_outstanding(&db, "Alice", 0, &outstanding) == 0);
    assert(outstanding == 0U);
    assert(memoserv_db_count(&db, "Bob", &count) == 0);
    assert(count == 1U);
    assert(memoserv_db_get(&db, "Bob", first, &memo) == 0);
    assert(memoserv_db_get(&db, "Bob", second, &memo) == 1);

    /* A future cutoff deterministically purges all remaining rows. */
    assert(memoserv_db_purge_before(&db, NULL, 4102444800LL, &deleted) == 0);
    assert(deleted == 1U);
    assert(memoserv_db_count(&db, "Bob", &count) == 0);
    assert(count == 0U);

    memoserv_db_close(&db);
    unlink(path);
    puts("memoserv database tests passed");
    return 0;
}
