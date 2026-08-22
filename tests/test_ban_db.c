/** @file test_ban_db.c @brief Unit tests for bans.db persistence and matching. */

#include "ban_db.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int count = 0;
static int count_record(const BanRecord *record, void *context) {
    (void)context;
    assert(record != NULL);
    ++count;
    return 0;
}

int main(void) {
    char path[128];
    BanDb db;
    BanRecord match;

    (void)snprintf(path, sizeof(path), "/tmp/scratchircd-bans-%ld.db", (long)getpid());
    unlink(path);

    assert(ban_db_open(&db, path) == 0);
    assert(ban_db_add(&db, BAN_TYPE_KLINE, "bad*@example.test",
                      "testing kline", "root") == 0);
    assert(ban_db_add(&db, BAN_TYPE_ZLINE, "192.0.2.*",
                      "testing zline", "root") == 0);

    assert(ban_db_match(&db, BAN_TYPE_KLINE,
                        "baduser@example.test", NULL, &match) == 1);
    assert(strcmp(match.reason, "testing kline") == 0);
    assert(match.expires_at == 0);
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "192.0.2.44", NULL, &match) == 1);
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "198.51.100.9", NULL, &match) == 0);

    /* Timed bans carry a real expiration and disappear once it is in the past. */
    assert(ban_db_add_timed(&db, BAN_TYPE_ZLINE, "203.0.113.9",
                            "temporary", "root", 60U) == 0);
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "203.0.113.9", NULL, &match) == 1);
    assert(match.expires_at > match.created_at);
    assert(sqlite3_exec(db.handle,
        "UPDATE bans SET expires_at=unixepoch()-1 WHERE type=2 AND mask='203.0.113.9'",
        NULL, NULL, NULL) == SQLITE_OK);
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "203.0.113.9", NULL, &match) == 0);

    count = 0;
    assert(ban_db_list(&db, BAN_TYPE_KLINE, count_record, NULL) == 0);
    assert(count == 1);

    assert(ban_db_delete(&db, BAN_TYPE_KLINE, "bad*@example.test") == 0);
    assert(ban_db_match(&db, BAN_TYPE_KLINE,
                        "baduser@example.test", NULL, &match) == 0);

    ban_db_close(&db);
    unlink(path);
    return 0;
}
