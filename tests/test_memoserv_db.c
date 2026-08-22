/** @file test_memoserv_db.c @brief Unit tests for MemoServ SQLite persistence. */
#include "memoserv_db.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/scratchircd-memoserv-XXXXXX";
    int fd = mkstemp(path);
    MemoServDb db = {0};
    MemoServMemo memos[8];
    MemoServMemo memo;
    size_t count = 0U;
    size_t unread = 0U;
    size_t deleted = 0U;
    long long first = 0;
    long long second = 0;
    long long third = 0;

    assert(fd >= 0);
    close(fd);
    unlink(path);

    assert(memoserv_db_open(&db, path) == 0);
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
