#include "chanserv_db.h"
#include "chanserv_persist.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void execf(sqlite3 *db, const char *sql) {
    assert(sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK);
}

static void raw_set_runtime_text(sqlite3 *db, const char *column, const char *value) {
    sqlite3_stmt *stmt = NULL;
    char sql[160];
    int written = snprintf(sql, sizeof(sql),
                           "UPDATE channel_runtime SET %s=?1 WHERE channel='#Persist'", column);
    assert(written > 0 && (size_t)written < sizeof(sql));
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_changes(db) == 1);
    sqlite3_finalize(stmt);
}

static void raw_set_runtime_int64(sqlite3 *db, const char *column, sqlite3_int64 value) {
    sqlite3_stmt *stmt = NULL;
    char sql[160];
    int written = snprintf(sql, sizeof(sql),
                           "UPDATE channel_runtime SET %s=?1 WHERE channel='#Persist'", column);
    assert(written > 0 && (size_t)written < sizeof(sql));
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_int64(stmt, 1, value);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_changes(db) == 1);
    sqlite3_finalize(stmt);
}

static void assert_restore_failed_clean(const char *path, Channel *channel) {
    assert(chanserv_persist_restore(path, channel) == -1);
    assert(channel->key[0] == '\0');
    assert(channel->user_limit == 0U);
    assert(channel->join_throttle_count == 0U);
    assert(channel->join_throttle_seconds == 0U);
    assert(channel->limit_redirect[0] == '\0');
    assert(channel->ban_redirect[0] == '\0');
    assert(channel->ban_list == NULL);
    assert(channel->exception_list == NULL);
    assert(channel->invite_exception_list == NULL);
}

int main(void) {
    char path[] = "/tmp/scratchircd-chanpersist-XXXXXX";
    int fd = mkstemp(path);
    ChanServDb db = {0};
    Channel source;
    Channel restored;
    char long_key[IRC_CHANNEL_KEY_MAX + 2U];
    char long_redirect[IRC_CHANNEL_NAME_MAX + 2U];
    char long_mask[IRC_CHANNEL_MASK_MAX + 2U];

    memset(long_key, 'K', IRC_CHANNEL_KEY_MAX + 1U);
    long_key[IRC_CHANNEL_KEY_MAX + 1U] = '\0';
    memset(long_redirect, 'R', IRC_CHANNEL_NAME_MAX + 1U);
    long_redirect[IRC_CHANNEL_NAME_MAX + 1U] = '\0';
    memset(long_mask, 'M', IRC_CHANNEL_MASK_MAX + 1U);
    long_mask[IRC_CHANNEL_MASK_MAX + 1U] = '\0';

    assert(fd >= 0);
    close(fd);
    unlink(path);

    assert(chanserv_db_open(&db, path) == 0);
    assert(chanserv_db_create(&db, "#Persist", "Alice", "runtime state") == 0);
    chanserv_db_close(&db);

    memset(&source, 0, sizeof(source));
    snprintf(source.name, sizeof(source.name), "%s", "#Persist");
    snprintf(source.key, sizeof(source.key), "%s", "secret");
    source.user_limit = 25U;
    source.join_throttle_count = 3U;
    source.join_throttle_seconds = 30U;
    snprintf(source.limit_redirect, sizeof(source.limit_redirect), "%s", "#overflow");
    snprintf(source.ban_redirect, sizeof(source.ban_redirect), "%s", "#banned");
    assert(channel_mask_add_authorized(&source.ban_list, "Bad!*@*", 1) == 0);
    assert(channel_mask_add(&source.exception_list, "Friend!*@*") == 0);
    assert(channel_mask_add(&source.invite_exception_list, "Invite!*@*") == 0);

    assert(chanserv_persist_save(path, &source) == 0);

    memset(&restored, 0, sizeof(restored));
    snprintf(restored.name, sizeof(restored.name), "%s", "#persist");
    assert(chanserv_persist_restore(path, &restored) == 0);
    assert(strcmp(restored.key, "secret") == 0);
    assert(restored.user_limit == 25U);
    assert(restored.join_throttle_count == 3U);
    assert(restored.join_throttle_seconds == 30U);
    assert(strcmp(restored.limit_redirect, "#overflow") == 0);
    assert(strcmp(restored.ban_redirect, "#banned") == 0);
    assert(restored.ban_list != NULL);
    assert(strcmp(restored.ban_list->mask, "Bad!*@*") == 0);
    assert(restored.ban_list->protected_authorized == 1);
    assert(restored.exception_list != NULL);
    assert(strcmp(restored.exception_list->mask, "Friend!*@*") == 0);
    assert(restored.invite_exception_list != NULL);
    assert(strcmp(restored.invite_exception_list->mask, "Invite!*@*") == 0);
    channel_mask_clear(&restored.ban_list);
    channel_mask_clear(&restored.exception_list);
    channel_mask_clear(&restored.invite_exception_list);

    assert(sqlite3_open(path, &db.db) == SQLITE_OK);

    raw_set_runtime_text(db.db, "channel_key", long_key);
    assert_restore_failed_clean(path, &restored);
    raw_set_runtime_text(db.db, "channel_key", "secret");

    raw_set_runtime_text(db.db, "limit_redirect", "#bad\nredirect");
    assert_restore_failed_clean(path, &restored);
    raw_set_runtime_text(db.db, "limit_redirect", "#overflow");

    raw_set_runtime_text(db.db, "ban_redirect", long_redirect);
    assert_restore_failed_clean(path, &restored);
    raw_set_runtime_text(db.db, "ban_redirect", "#banned");

    raw_set_runtime_int64(db.db, "user_limit", -1);
    assert_restore_failed_clean(path, &restored);
    raw_set_runtime_int64(db.db, "user_limit", 25);

    raw_set_runtime_int64(db.db, "join_count", -1);
    assert_restore_failed_clean(path, &restored);
    raw_set_runtime_int64(db.db, "join_count", 3);

    raw_set_runtime_int64(db.db, "join_seconds", (sqlite3_int64)UINT_MAX + 1LL);
    assert_restore_failed_clean(path, &restored);
    raw_set_runtime_int64(db.db, "join_seconds", 30);

    {
        sqlite3_stmt *stmt = NULL;
        assert(sqlite3_prepare_v2(db.db,
            "UPDATE channel_masks SET mask=?1 WHERE channel='#Persist' AND type=1",
            -1, &stmt, NULL) == SQLITE_OK);
        sqlite3_bind_text(stmt, 1, long_mask, -1, SQLITE_TRANSIENT);
        assert(sqlite3_step(stmt) == SQLITE_DONE);
        assert(sqlite3_changes(db.db) == 1);
        sqlite3_finalize(stmt);
    }
    assert_restore_failed_clean(path, &restored);
    execf(db.db, "UPDATE channel_masks SET mask='Bad!*@*' WHERE channel='#Persist' AND type=1");

    execf(db.db, "UPDATE channel_masks SET protected_authorized=2 WHERE channel='#Persist' AND type=1");
    assert_restore_failed_clean(path, &restored);
    execf(db.db, "UPDATE channel_masks SET protected_authorized=1 WHERE channel='#Persist' AND type=1");

    /* CHECK normally protects the type field; emulate external schema damage by
     * rebuilding one row with foreign-key checks disabled in a scratch table. */
    execf(db.db, "PRAGMA foreign_keys=OFF");
    execf(db.db, "CREATE TABLE bad_masks(channel TEXT,type INTEGER,mask TEXT,protected_authorized INTEGER)");
    execf(db.db, "INSERT INTO bad_masks VALUES('#Persist',99,'Bad!*@*',1)");
    execf(db.db, "DELETE FROM channel_masks WHERE channel='#Persist' AND type=1");
    execf(db.db, "PRAGMA ignore_check_constraints=ON");
    execf(db.db, "INSERT INTO channel_masks SELECT * FROM bad_masks");
    execf(db.db, "PRAGMA ignore_check_constraints=OFF");
    assert_restore_failed_clean(path, &restored);
    execf(db.db, "DELETE FROM channel_masks WHERE channel='#Persist' AND type=99");
    execf(db.db, "INSERT INTO channel_masks(channel,type,mask,protected_authorized) VALUES('#Persist',1,'Bad!*@*',1)");
    execf(db.db, "DROP TABLE bad_masks");

    sqlite3_close(db.db);
    db.db = NULL;

    assert(chanserv_persist_restore(path, &restored) == 0);
    assert(strcmp(restored.key, "secret") == 0);
    assert(restored.user_limit == 25U);
    assert(restored.join_throttle_count == 3U);
    assert(restored.join_throttle_seconds == 30U);
    assert(restored.ban_list != NULL);

    channel_mask_clear(&source.ban_list);
    channel_mask_clear(&source.exception_list);
    channel_mask_clear(&source.invite_exception_list);
    channel_mask_clear(&restored.ban_list);
    channel_mask_clear(&restored.exception_list);
    channel_mask_clear(&restored.invite_exception_list);
    chanserv_persist_reset();
    unlink(path);
    puts("chanserv persistence tests passed");
    return 0;
}
