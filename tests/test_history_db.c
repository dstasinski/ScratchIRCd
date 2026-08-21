/** @file test_history_db.c @brief Unit coverage for SQLite channel history. */
#include "history_db.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[256];
    HistoryDb db = {0};
    HistoryRecord record;
    HistoryRecord rows[4];
    size_t count = 0U;

    (void)snprintf(path, sizeof(path), "/tmp/scratchircd-history-%ld.db", (long)getpid());
    (void)unlink(path);
    assert(history_db_open(&db, path) == 0);

    memset(&record, 0, sizeof(record));
    (void)snprintf(record.target, sizeof(record.target), "#Test");
    (void)snprintf(record.command, sizeof(record.command), "PRIVMSG");
    (void)snprintf(record.nick, sizeof(record.nick), "Alice");
    (void)snprintf(record.user, sizeof(record.user), "alice");
    (void)snprintf(record.host, sizeof(record.host), "cloak.example");
    (void)snprintf(record.account, sizeof(record.account), "Alice");
    (void)snprintf(record.text, sizeof(record.text), "first");
    record.created_at_ms = 1000;
    assert(history_db_add(&db, &record) == 0);

    (void)snprintf(record.command, sizeof(record.command), "NOTICE");
    (void)snprintf(record.text, sizeof(record.text), "second");
    record.created_at_ms = 2000;
    assert(history_db_add(&db, &record) == 0);

    (void)snprintf(record.command, sizeof(record.command), "PRIVMSG");
    (void)snprintf(record.text, sizeof(record.text), "third");
    record.created_at_ms = 3000;
    assert(history_db_add(&db, &record) == 0);

    memset(rows, 0, sizeof(rows));
    assert(history_db_latest(&db, "#test", 2U, rows, 4U, &count) == 0);
    assert(count == 2U);
    assert(strcmp(rows[0].text, "second") == 0);
    assert(strcmp(rows[0].command, "NOTICE") == 0);
    assert(strcmp(rows[1].text, "third") == 0);
    assert(rows[0].created_at_ms < rows[1].created_at_ms);

    history_db_close(&db);
    (void)unlink(path);
    return 0;
}
