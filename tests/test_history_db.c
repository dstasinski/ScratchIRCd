/** @file test_history_db.c @brief Unit coverage for SQLite channel history. */
#include "history_db.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void clear_history(HistoryDb *db) {
    assert(db != NULL && db->handle != NULL);
    assert(sqlite3_exec(db->handle, "DELETE FROM history", NULL, NULL, NULL) == SQLITE_OK);
}

static void inject_history_row(HistoryDb *db, const char *command,
                               const char *text, int text_bytes) {
    sqlite3_stmt *stmt = NULL;
    assert(db != NULL && db->handle != NULL);
    assert(sqlite3_prepare_v2(db->handle,
        "INSERT INTO history(target,command,nick,user,host,account,text,created_at_ms) "
        "VALUES('#test',?1,'Alice','alice','cloak.example','Alice',?2,1000)",
        -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, command, -1, SQLITE_TRANSIENT);
    if (text_bytes >= 0)
        sqlite3_bind_text(stmt, 2, text, text_bytes, SQLITE_TRANSIENT);
    else
        sqlite3_bind_text(stmt, 2, text, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

int main(void) {
    char path[256];
    char path2[256];
    char long_text[IRCD_HISTORY_TEXT_MAX + 2U];
    char embedded_text[] = {'a', 'b', '\0', 'c', 'd'};
    HistoryDb db = {0};
    HistoryDb *shared1;
    HistoryDb *shared2;
    HistoryRecord record;
    HistoryRecord malformed;
    HistoryRecord rows[8];
    size_t count = 0U;
    int i;

    (void)snprintf(path, sizeof(path), "/tmp/scratchircd-history-%ld.db", (long)getpid());
    (void)snprintf(path2, sizeof(path2), "/tmp/scratchircd-history2-%ld.db", (long)getpid());
    (void)unlink(path);
    (void)unlink(path2);
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

    /* The public writer validates fixed arrays before any unbounded string
     * operation or SQLite bind. Malformed internal records must be refused. */
    malformed = record;
    memset(malformed.command, 'X', sizeof(malformed.command));
    assert(history_db_add(&db, &malformed) != 0);
    malformed = record;
    (void)snprintf(malformed.text, sizeof(malformed.text), "bad\nline");
    assert(history_db_add(&db, &malformed) != 0);
    malformed = record;
    (void)snprintf(malformed.host, sizeof(malformed.host), "bad\rhost");
    assert(history_db_add(&db, &malformed) != 0);

    memset(rows, 0, sizeof(rows));
    assert(history_db_latest(&db, "#test", 2U, rows, 8U, &count) == 0);
    assert(count == 2U);
    assert(strcmp(rows[0].text, "second") == 0);
    assert(strcmp(rows[1].text, "third") == 0);

    /* Age pruning removes rows older than the configured window. */
    assert(history_db_prune(&db, 1U, 100U, 86400000LL + 2500LL) == 0);
    count = 0U;
    assert(history_db_latest(&db, "#test", 8U, rows, 8U, &count) == 0);
    assert(count == 1U);
    assert(strcmp(rows[0].text, "third") == 0);

    /* Global row pruning keeps only the newest max_rows records. */
    for (i = 0; i < 6; ++i) {
        (void)snprintf(record.text, sizeof(record.text), "row-%d", i);
        record.created_at_ms = 10000 + i;
        assert(history_db_add(&db, &record) == 0);
    }
    assert(history_db_prune(&db, 0U, 3U, 20000) == 0);
    count = 0U;
    assert(history_db_latest(&db, "#test", 8U, rows, 8U, &count) == 0);
    assert(count == 3U);
    assert(strcmp(rows[0].text, "row-3") == 0);
    assert(strcmp(rows[2].text, "row-5") == 0);

    /* Bypass the public writer to simulate corrupt or legacy SQLite rows.
     * Reads must fail closed rather than truncating or replaying malformed IRC. */
    clear_history(&db);
    memset(long_text, 'X', sizeof(long_text) - 1U);
    long_text[sizeof(long_text) - 1U] = '\0';
    assert(strlen(long_text) == IRCD_HISTORY_TEXT_MAX + 1U);
    inject_history_row(&db, "PRIVMSG", long_text, -1);
    count = 99U;
    assert(history_db_latest(&db, "#test", 8U, rows, 8U, &count) != 0);
    assert(count == 0U);

    clear_history(&db);
    inject_history_row(&db, "PRIVMSG", embedded_text, (int)sizeof(embedded_text));
    count = 99U;
    assert(history_db_latest(&db, "#test", 8U, rows, 8U, &count) != 0);
    assert(count == 0U);

    clear_history(&db);
    inject_history_row(&db, "PRIVMSG", "bad\nline", -1);
    count = 99U;
    assert(history_db_latest(&db, "#test", 8U, rows, 8U, &count) != 0);
    assert(count == 0U);

    clear_history(&db);
    inject_history_row(&db, "PRIVMSG", "bad\rline", -1);
    count = 99U;
    assert(history_db_latest(&db, "#test", 8U, rows, 8U, &count) != 0);
    assert(count == 0U);

    clear_history(&db);
    inject_history_row(&db, "JOIN", "not a message", -1);
    count = 99U;
    assert(history_db_latest(&db, "#test", 8U, rows, 8U, &count) != 0);
    assert(count == 0U);

    history_db_close(&db);

    /* The shared API keeps one open handle for repeated hot-path calls. */
    shared1 = history_db_shared(path);
    assert(shared1 != NULL && shared1->handle != NULL);
    shared2 = history_db_shared(path);
    assert(shared2 == shared1);
    assert(history_db_shared_maintain(path, 0U, 3U, 30000) == 0);

    /* A restart selecting another history path transparently replaces it. */
    shared2 = history_db_shared(path2);
    assert(shared2 == shared1 && shared2->handle != NULL);
    count = 99U;
    assert(history_db_latest(shared2, "#test", 3U, rows, 8U, &count) == 0);
    assert(count == 0U);

    (void)unlink(path);
    (void)unlink(path2);
    return 0;
}
