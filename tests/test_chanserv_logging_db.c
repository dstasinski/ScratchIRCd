#define _POSIX_C_SOURCE 200809L

#include "chanserv_db.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void inject_text_row(ChanServDb *db, const char *channel,
                            const char *body, int body_bytes) {
    sqlite3_stmt *stmt = NULL;
    assert(db != NULL && db->db != NULL);
    assert(sqlite3_prepare_v2(db->db,
        "INSERT INTO channel_log_queue(channel,event_time,body) VALUES(?1,100,?2)",
        -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, channel, -1, SQLITE_TRANSIENT);
    if (body_bytes >= 0)
        sqlite3_bind_text(stmt, 2, body, body_bytes, SQLITE_TRANSIENT);
    else
        sqlite3_bind_text(stmt, 2, body, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

static void clear_queue(ChanServDb *db) {
    assert(sqlite3_exec(db->db, "DELETE FROM channel_log_queue",
                        NULL, NULL, NULL) == SQLITE_OK);
}

int main(void) {
    char path[128];
    char long_channel[IRC_CHANNEL_NAME_MAX + 2U];
    char long_body[sizeof(((ChanServLogQueueRecord *)0)->body) + 1U];
    char embedded_body[] = {'a', 'b', '\0', 'c', 'd'};
    char list_buffer[4];
    ChanServDb db = {0};
    ChanServLogQueueRecord rows[4];
    size_t count = 0U;

    (void)snprintf(path, sizeof(path), "/tmp/scratchircd-chanlog-db-%ld.db",
                   (long)getpid());
    unlink(path);

    assert(chanserv_db_open(&db, path) == 0);
    assert(chanserv_db_logging_ensure_schema(&db) == 0);

    assert(chanserv_db_logging_queue_add(&db, "#ok", 100, "normal body") == 0);
    assert(chanserv_db_logging_queue_fetch_due(&db, 100, rows, 4, &count) == 0);
    assert(count == 1U);
    assert(strcmp(rows[0].channel, "#ok") == 0);
    assert(strcmp(rows[0].body, "normal body") == 0);
    clear_queue(&db);

    memset(long_channel, 'c', sizeof(long_channel) - 1U);
    long_channel[0] = '#';
    long_channel[sizeof(long_channel) - 1U] = '\0';
    assert(strlen(long_channel) == IRC_CHANNEL_NAME_MAX + 1U);
    assert(chanserv_db_logging_queue_add(&db, long_channel, 100, "body") != 0);

    memset(long_body, 'b', sizeof(long_body) - 1U);
    long_body[sizeof(long_body) - 1U] = '\0';
    assert(strlen(long_body) == sizeof(((ChanServLogQueueRecord *)0)->body));
    assert(chanserv_db_logging_queue_add(&db, "#ok", 100, long_body) != 0);

    /* Log records are line-oriented and may later appear in diagnostics. IRC
     * input cannot legitimately contain CR/LF, so reject them at persistence. */
    assert(chanserv_db_logging_queue_add(&db, "#bad\rname", 100, "body") != 0);
    assert(chanserv_db_logging_queue_add(&db, "#bad\nname", 100, "body") != 0);
    assert(chanserv_db_logging_queue_add(&db, "#ok", 100, "bad\rbody") != 0);
    assert(chanserv_db_logging_queue_add(&db, "#ok", 100, "bad\nbody") != 0);

    /* Bypass the public write bounds to simulate a corrupt/legacy row. Fetch
     * must fail instead of clipping the text into ChanServLogQueueRecord. */
    inject_text_row(&db, "#ok", long_body, -1);
    count = 99U;
    assert(chanserv_db_logging_queue_fetch_due(&db, 100, rows, 4, &count) != 0);
    assert(count == 0U);
    assert(chanserv_db_logging_queue_count(&db, &count) == 0 && count == 1U);
    clear_queue(&db);

    inject_text_row(&db, "#ok", embedded_body, (int)sizeof(embedded_body));
    count = 99U;
    assert(chanserv_db_logging_queue_fetch(&db, "#ok", rows, 4, &count) != 0);
    assert(count == 0U);
    assert(chanserv_db_logging_queue_count(&db, &count) == 0 && count == 1U);
    clear_queue(&db);

    inject_text_row(&db, "#ok", "bad\nbody", -1);
    count = 99U;
    assert(chanserv_db_logging_queue_fetch_due(&db, 100, rows, 4, &count) != 0);
    assert(count == 0U);
    assert(chanserv_db_logging_queue_count(&db, &count) == 0 && count == 1U);
    clear_queue(&db);

    inject_text_row(&db, "#bad\rname", "body", -1);
    count = 99U;
    assert(chanserv_db_logging_queue_fetch_due(&db, 100, rows, 4, &count) != 0);
    assert(count == 0U);
    assert(chanserv_db_logging_queue_count(&db, &count) == 0 && count == 1U);
    clear_queue(&db);

    /* Legacy list API must never report a successful partial channel list. */
    assert(chanserv_db_logging_queue_add(&db, "#a", 100, "one") == 0);
    assert(chanserv_db_logging_queue_add(&db, "#b", 101, "two") == 0);
    memset(list_buffer, 'x', sizeof(list_buffer));
    assert(chanserv_db_logging_queue_list_channels(&db, list_buffer,
                                                    sizeof(list_buffer)) != 0);
    assert(list_buffer[0] == '\0');

    chanserv_db_close(&db);
    unlink(path);
    return 0;
}
