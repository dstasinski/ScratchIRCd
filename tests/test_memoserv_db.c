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

int main(void) {
    char path[] = "/tmp/scratchircd-memoserv-XXXXXX";
    int fd = mkstemp(path);
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

    fill_overlong(long_account, IRC_NICK_MAX, 'A');
    fill_overlong(long_text, IRCD_MEMOSERV_TEXT_MAX, 'M');

    assert(fd >= 0);
    close(fd);
    unlink(path);

    assert(memoserv_db_open(&db, path) == 0);

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

    /* Legacy/external corruption must fail closed rather than returning a
     * clipped or multi-line sender, recipient, or memo body. */
    raw_set_memo_text(db.handle, "text", first, long_text);
    assert(memoserv_db_get(&db, "Bob", first, &memo) == -1);
    assert(memoserv_db_list(&db, "Bob", memos, 8U, &count) == -1);
    assert(memoserv_db_list_sent(&db, "Alice", memos, 8U, &count) == -1);
    raw_set_memo_text(db.handle, "text", first, "first memo");
    assert(memoserv_db_get(&db, "Bob", first, &memo) == 1);

    raw_set_memo_text(db.handle, "text", first, "first\nmemo");
    assert(memoserv_db_get(&db, "Bob", first, &memo) == -1);
    assert(memoserv_db_list(&db, "Bob", memos, 8U, &count) == -1);
    raw_set_memo_text(db.handle, "text", first, "first memo");
    assert(memoserv_db_get(&db, "Bob", first, &memo) == 1);

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

    assert(memoserv_db_mark_read(&db, "Bob", first, 12345) == 0);
    assert(memoserv_db_unread_count(&db, "Bob", &unread) == 0);
    assert(unread == 1U);
    assert(memoserv_db_get(&db, "Bob", first, &memo) == 1);
    assert(memo.read_at == 12345);

    /* A future cutoff deterministically purges all remaining rows. */
    assert(memoserv_db_purge_before(&db, NULL, 4102444800LL, &deleted) == 0);
    assert(deleted == 3U);
    assert(memoserv_db_count(&db, "Bob", &count) == 0);
    assert(count == 0U);

    memoserv_db_close(&db);
    unlink(path);
    puts("memoserv database tests passed");
    return 0;
}