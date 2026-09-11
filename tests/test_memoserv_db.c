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

static void create_legacy_memoserv_db(const char *path) {
    sqlite3 *legacy = NULL;
    char *error = NULL;
    static const char sql[] =
        "CREATE TABLE memos ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "sender TEXT COLLATE NOCASE NOT NULL,"
        "recipient TEXT COLLATE NOCASE NOT NULL,"
        "text TEXT NOT NULL,"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "read_at INTEGER NOT NULL DEFAULT 0"
        ");"
        "CREATE INDEX memos_recipient_id ON memos(recipient,id DESC);"
        "CREATE INDEX memos_recipient_unread ON memos(recipient,read_at);"
        "CREATE INDEX memos_sender_id ON memos(sender,id DESC);"
        "INSERT INTO memos(sender,recipient,text,created_at,read_at) "
        "VALUES('Alice','Bob','legacy memo',12345,0);";

    assert(sqlite3_open(path, &legacy) == SQLITE_OK);
    assert(sqlite3_exec(legacy, sql, NULL, NULL, &error) == SQLITE_OK);
    assert(error == NULL);
    assert(!raw_column_exists(legacy, "sender_deleted"));
    sqlite3_close(legacy);
}

int main(void) {
    char path[] = "/tmp/scratchircd-memoserv-XXXXXX";
    char legacy_path[] = "/tmp/scratchircd-memoserv-legacy-XXXXXX";
    int fd = mkstemp(path);
    int legacy_fd = mkstemp(legacy_path);
    MemoServDb db = {0};
    MemoServMemo memos[8];
    MemoServMemo memo;
    char long_account[IRC_NICK_MAX + 2U];
    char long_text[IRCD_MEMOSERV_TEXT_MAX + 2U];
    size_t count = 0U;
    size_t unread = 0U;
    size_t deleted = 0U;
    long long first = 0;
    long long second = 0;
    long long third = 0;
    long long fourth = 0;

    fill_overlong(long_account, IRC_NICK_MAX, 'A');
    fill_overlong(long_text, IRCD_MEMOSERV_TEXT_MAX, 'M');

    assert(fd >= 0);
    assert(legacy_fd >= 0);
    close(fd);
    close(legacy_fd);
    unlink(path);
    unlink(legacy_path);

    create_legacy_memoserv_db(legacy_path);
    assert(memoserv_db_open(&db, legacy_path) == 0);
    assert(raw_column_exists(db.handle, "sender_deleted"));
    assert(raw_index_exists(db.handle, "memos_sender_visible_id"));
    assert(memoserv_db_list_sent(&db, "alice", memos, 8U, &count) == 0);
    assert(count == 1U);
    assert(memos[0].id == 1);
    assert(strcmp(memos[0].recipient, "Bob") == 0);
    assert(memoserv_db_delete_sent(&db, "Alice", 1) == 1);
    assert(memoserv_db_list_sent(&db, "Alice", memos, 8U, &count) == 0);
    assert(count == 0U);
    assert(memoserv_db_get(&db, "Bob", 1, &memo) == 1);
    assert(strcmp(memo.text, "legacy memo") == 0);
    memoserv_db_close(&db);
    unlink(legacy_path);

    assert(memoserv_db_open(&db, path) == 0);
    assert(raw_index_exists(db.handle, "memos_sender_visible_id"));

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

    /* Sent-history deletion is sender-owned visibility only. The recipient's
     * memo remains stored and readable. */
    assert(memoserv_db_delete_sent(&db, "Mallory", third) == 0);
    assert(memoserv_db_delete_sent(&db, "ALICE", third) == 1);
    assert(memoserv_db_get_sent(&db, "Alice", third, &memo) == 0);
    assert(memoserv_db_count(&db, "Dave", &count) == 0);
    assert(count == 1U);
    assert(memoserv_db_get(&db, "Dave", third, &memo) == 1);
    assert(strcmp(memo.text, "sent memo") == 0);
    assert(memoserv_db_count(&db, "Bob", &count) == 0);
    assert(count == 2U);

    assert(memoserv_db_send(&db, "Alice", "Erin", "another sent memo", &fourth) == 0);
    assert(fourth > third);
    assert(memoserv_db_delete_all_sent(&db, "ALICE") == 0);
    assert(memoserv_db_list_sent(&db, "Alice", memos, 8U, &count) == 0);
    assert(count == 0U);
    assert(memoserv_db_count(&db, "Bob", &count) == 0);
    assert(count == 2U);
    assert(memoserv_db_get(&db, "Bob", first, &memo) == 1);
    assert(memoserv_db_count(&db, "Erin", &count) == 0);
    assert(count == 1U);

    /* Recreate an Alice-owned visible sent row for sent corruption and
     * account-purge checks. */
    assert(memoserv_db_send(&db, "Alice", "Dave", "sent memo", &third) == 0);

    /* Legacy/external corruption must fail closed rather than returning a
     * clipped or multi-line sender, recipient, or memo body. */
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
    assert(unread == 1U);
    assert(memoserv_db_get(&db, "Bob", second, &memo) == 1);
    assert(memo.read_at == 12345);

    /* Dropped account cleanup physically removes both inbox and sent rows,
     * including sender-hidden rows. */
    assert(memoserv_db_delete_account(&db, "ALICE", &deleted) == 0);
    assert(deleted == 4U);
    assert(memoserv_db_list_sent(&db, "Alice", memos, 8U, &count) == 0);
    assert(count == 0U);
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
