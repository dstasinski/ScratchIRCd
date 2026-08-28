/** @file test_ban_db.c @brief Unit tests for bans.db persistence and matching. */

#include "ban_db.h"
#include "sqlite_policy.h"

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

static int pragma_int(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt = NULL;
    int value = -1;
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

static void inject_ban_row(sqlite3 *db, int type,
                           const char *mask, int mask_bytes,
                           const char *reason, int reason_bytes,
                           const char *set_by, int set_by_bytes) {
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(db,
        "INSERT INTO bans(type,mask,reason,set_by,created_at,expires_at) "
        "VALUES(?1,?2,?3,?4,unixepoch(),0)",
        -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_int(stmt, 1, type);
    sqlite3_bind_text(stmt, 2, mask, mask_bytes, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, reason, reason_bytes, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, set_by, set_by_bytes, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

static void clear_injected(sqlite3 *db, const char *mask) {
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(db, "DELETE FROM bans WHERE mask=?1",
                              -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, mask, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

int main(void) {
    char path[128];
    BanDb db;
    BanRecord match;
    sqlite3 *locker = NULL;
    char oversized_mask[IRC_CHANNEL_MASK_MAX + 2U];
    char oversized_reason[IRC_QUIT_REASON_MAX + 2U];
    char oversized_set_by[IRCD_OPER_NAME_MAX + 2U];
    char embedded_setter[] = {'r','o','o','t','\0','x'};

    (void)snprintf(path, sizeof(path), "/tmp/scratchircd-bans-%ld.db", (long)getpid());
    unlink(path);

    assert(ban_db_open(&db, path) == 0);
    assert(pragma_int(db.handle, "PRAGMA busy_timeout") == IRCD_SQLITE_BUSY_TIMEOUT_MS);
    assert(pragma_int(db.handle, "PRAGMA user_version") == 1);
    assert(pragma_int(db.handle, "PRAGMA synchronous") == 1);
    assert(ban_db_add(&db, BAN_TYPE_KLINE, "bad*@example.test",
                      "testing kline", "root") == 0);
    assert(ban_db_add(&db, BAN_TYPE_ZLINE, "192.0.2.*",
                      "testing legacy wildcard zline", "root") == 0);
    assert(ban_db_add(&db, BAN_TYPE_ZLINE, "203.0.113.0/24",
                      "testing ipv4 cidr zline", "root") == 0);
    assert(ban_db_add(&db, BAN_TYPE_ZLINE, "2001:db8:1234::/48",
                      "testing ipv6 cidr zline", "root") == 0);
    assert(ban_db_add(&db, BAN_TYPE_ZLINE, "2001:db8::1",
                      "testing exact ipv6 zline", "root") == 0);

    /* Persistence rejects fields that cannot round-trip through BanRecord. */
    memset(oversized_mask, 'm', sizeof(oversized_mask) - 1U);
    oversized_mask[sizeof(oversized_mask) - 1U] = '\0';
    memset(oversized_reason, 'r', sizeof(oversized_reason) - 1U);
    oversized_reason[sizeof(oversized_reason) - 1U] = '\0';
    memset(oversized_set_by, 's', sizeof(oversized_set_by) - 1U);
    oversized_set_by[sizeof(oversized_set_by) - 1U] = '\0';
    assert(ban_db_add(&db, BAN_TYPE_KLINE, oversized_mask, "reason", "root") == -1);
    assert(ban_db_add(&db, BAN_TYPE_KLINE, "*@oversized-reason.test",
                      oversized_reason, "root") == -1);
    assert(ban_db_add(&db, BAN_TYPE_KLINE, "*@oversized-setter.test",
                      "reason", oversized_set_by) == -1);
    assert(ban_db_add(&db, (BanType)99, "*@invalid-type.test", "reason", "root") == -1);
    assert(ban_db_add(&db, BAN_TYPE_KLINE, "*@bad\nmask.test", "reason", "root") == -1);
    assert(ban_db_add(&db, BAN_TYPE_KLINE, "*@bad-reason.test", "bad\rreason", "root") == -1);
    assert(ban_db_add(&db, BAN_TYPE_KLINE, "*@bad-setter.test", "reason", "root\nother") == -1);
    assert(ban_db_delete(&db, (BanType)99, "*@invalid.test") == -1);
    assert(ban_db_list(&db, (BanType)99, count_record, NULL) == -1);
    assert(ban_db_match(&db, (BanType)99, "x@y", NULL, &match) == -1);

    assert(ban_db_match(&db, BAN_TYPE_KLINE,
                        "baduser@example.test", NULL, &match) == 1);
    assert(strcmp(match.reason, "testing kline") == 0);
    assert(match.expires_at == 0);
    assert(ban_record_matches(&match, "baduser@example.test", NULL));
    assert(!ban_record_matches(&match, "gooduser@example.test", NULL));

    /* Matching must remain a WAL-safe read operation even while another
     * connection owns the database writer slot. The old pre-match DELETE
     * purge made ordinary client ban checks contend for that writer lock. */
    assert(sqlite3_open(path, &locker) == SQLITE_OK);
    assert(sqlite3_exec(locker, "BEGIN IMMEDIATE", NULL, NULL, NULL) == SQLITE_OK);
    assert(ban_db_match(&db, BAN_TYPE_KLINE,
                        "baduser@example.test", NULL, &match) == 1);
    assert(strcmp(match.reason, "testing kline") == 0);
    assert(sqlite3_exec(locker, "ROLLBACK", NULL, NULL, NULL) == SQLITE_OK);
    sqlite3_close(locker);
    locker = NULL;

    /* Legacy wildcard ZLINEs continue to work. */
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "192.0.2.44", NULL, &match) == 1);
    assert(strcmp(match.reason, "testing legacy wildcard zline") == 0);
    assert(ban_record_matches(&match, "192.0.2.44", NULL));

    /* IPv4 CIDR matching is numeric and respects prefix boundaries. */
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "203.0.113.25", NULL, &match) == 1);
    assert(strcmp(match.mask, "203.0.113.0/24") == 0);
    assert(ban_record_matches(&match, "203.0.113.25", NULL));
    assert(!ban_record_matches(&match, "203.0.114.25", NULL));
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "203.0.114.25", NULL, &match) == 0);

    /* IPv6 CIDR matching accepts equivalent textual forms numerically. */
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "2001:0db8:1234:abcd::1", NULL, &match) == 1);
    assert(strcmp(match.mask, "2001:db8:1234::/48") == 0);
    assert(ban_record_matches(&match, "2001:0db8:1234:abcd::1", NULL));
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "2001:db8:1235::1", NULL, &match) == 0);

    /* Exact IPv6 ZLINEs also compare numerically, not by presentation text. */
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "2001:0db8:0:0:0:0:0:1", NULL, &match) == 1);
    assert(strcmp(match.mask, "2001:db8::1") == 0);
    assert(ban_record_matches(&match, "2001:0db8:0:0:0:0:0:1", NULL));

    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "198.51.100.9", NULL, &match) == 0);

    /* Timed bans carry a real expiration and disappear once it is in the past. */
    assert(ban_db_add_timed(&db, BAN_TYPE_ZLINE, "203.0.114.9",
                            "temporary", "root", 60U) == 0);
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "203.0.114.9", NULL, &match) == 1);
    assert(match.expires_at > match.created_at);
    assert(sqlite3_exec(db.handle,
        "UPDATE bans SET expires_at=unixepoch()-1 WHERE type=2 AND mask='203.0.114.9'",
        NULL, NULL, NULL) == SQLITE_OK);
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "203.0.114.9", NULL, &match) == 0);

    count = 0;
    assert(ban_db_list(&db, BAN_TYPE_KLINE, count_record, NULL) == 0);
    assert(count == 1);

    /* Direct SQLite corruption must fail closed. It may not be truncated into
     * a different effective policy row or delivered to LIST callbacks. */
    inject_ban_row(db.handle, BAN_TYPE_KLINE, oversized_mask, -1,
                   "reason", -1, "root", -1);
    assert(ban_db_match(&db, BAN_TYPE_KLINE, "anything@example.test", NULL, &match) == -1);
    assert(ban_db_list(&db, BAN_TYPE_KLINE, count_record, NULL) == -1);
    clear_injected(db.handle, oversized_mask);

    inject_ban_row(db.handle, BAN_TYPE_KLINE, "*@corrupt-reason.test", -1,
                   oversized_reason, -1, "root", -1);
    assert(ban_db_match(&db, BAN_TYPE_KLINE, "user@corrupt-reason.test", NULL, &match) == -1);
    clear_injected(db.handle, "*@corrupt-reason.test");

    inject_ban_row(db.handle, BAN_TYPE_KLINE, "*@corrupt-nul.test", -1,
                   "reason", -1, embedded_setter, (int)sizeof(embedded_setter));
    assert(ban_db_match(&db, BAN_TYPE_KLINE, "user@corrupt-nul.test", NULL, &match) == -1);
    clear_injected(db.handle, "*@corrupt-nul.test");

    inject_ban_row(db.handle, BAN_TYPE_ZLINE, "198.51.100.77", -1,
                   "bad\nreason", -1, "root", -1);
    assert(ban_db_match(&db, BAN_TYPE_ZLINE, "198.51.100.77", NULL, &match) == -1);
    clear_injected(db.handle, "198.51.100.77");

    assert(ban_db_delete(&db, BAN_TYPE_ZLINE, "203.0.113.0/24") == 0);
    assert(ban_db_match(&db, BAN_TYPE_ZLINE,
                        "203.0.113.25", NULL, &match) == 0);

    assert(ban_db_delete(&db, BAN_TYPE_KLINE, "bad*@example.test") == 0);
    assert(ban_db_match(&db, BAN_TYPE_KLINE,
                        "baduser@example.test", NULL, &match) == 0);

    ban_db_close(&db);
    assert(ban_db_open(&db, path) == 0);
    assert(pragma_int(db.handle, "PRAGMA user_version") == 1);
    ban_db_close(&db);
    unlink(path);
    return 0;
}
