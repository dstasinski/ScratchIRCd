#include "chanserv_db.h"
#include "modes.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int pragma_int(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt = NULL;
    int value = -1;
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

static void raw_set_channel_text(sqlite3 *db, const char *column,
                                 const char *name, const char *value) {
    sqlite3_stmt *stmt = NULL;
    char sql[160];
    int written = snprintf(sql, sizeof(sql),
                           "UPDATE channels SET %s=?1 WHERE name=?2", column);
    assert(written > 0 && (size_t)written < sizeof(sql));
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_changes(db) == 1);
    sqlite3_finalize(stmt);
}

static void raw_set_access_account(sqlite3 *db, const char *channel,
                                   const char *old_account,
                                   const char *new_account) {
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(db,
        "UPDATE access SET account=?1 WHERE channel=?2 AND account=?3",
        -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, new_account, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, channel, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, old_account, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_changes(db) == 1);
    sqlite3_finalize(stmt);
}

static void fill_overlong(char *buffer, size_t valid_max, char ch) {
    memset(buffer, ch, valid_max + 1U);
    buffer[valid_max + 1U] = '\0';
}

int main(void) {
    char path[] = "/tmp/scratchircd-chanserv-XXXXXX";
    int fd = mkstemp(path);
    ChanServDb db = {0};
    ChanServChannel record;
    ChanServAccess access;
    char list[256];
    char long_name[IRC_CHANNEL_NAME_MAX + 2U];
    char long_founder[IRC_NICK_MAX + 2U];
    char long_description[IRCD_CHANSERV_DESCRIPTION_MAX + 2U];
    char long_topic[IRC_CHANNEL_TOPIC_MAX + 2U];
    char long_setter[IRC_CHANNEL_TOPIC_SETTER_MAX + 2U];
    char long_account[IRC_NICK_MAX + 2U];
    uint64_t generation;

    fill_overlong(long_name, IRC_CHANNEL_NAME_MAX, 'N');
    long_name[0] = '#';
    fill_overlong(long_founder, IRC_NICK_MAX, 'F');
    fill_overlong(long_description, IRCD_CHANSERV_DESCRIPTION_MAX, 'D');
    fill_overlong(long_topic, IRC_CHANNEL_TOPIC_MAX, 'T');
    fill_overlong(long_setter, IRC_CHANNEL_TOPIC_SETTER_MAX, 'S');
    fill_overlong(long_account, IRC_NICK_MAX, 'A');

    assert(fd >= 0);
    close(fd);
    unlink(path);

    assert(chanserv_db_open(&db, path) == 0);
    assert(pragma_int(db.db, "PRAGMA user_version") == 1);
    assert(pragma_int(db.db, "PRAGMA busy_timeout") == 250);
    assert(pragma_int(db.db, "PRAGMA synchronous") == 1);

    /* Public DB APIs must never create rows that their fixed-size record
     * structures cannot later decode losslessly. Invalid attempts must not
     * consume registration capacity. */
    assert(chanserv_db_create(&db, long_name, "Alice", "bad name") == -1);
    assert(chanserv_db_create(&db, "#BadFounder", long_founder, "bad founder") == -1);
    assert(chanserv_db_create(&db, "#BadDescription", "Alice", long_description) == -1);

    generation = chanserv_db_pchannels_generation();
    assert(chanserv_db_create(&db, "#Test", "Alice", "Example channel") == 0);
    assert(chanserv_db_pchannels_generation() != generation);
    generation = chanserv_db_pchannels_generation();

    assert(chanserv_db_set_founder(&db, "#Test", long_founder) == -1);
    assert(chanserv_db_set_description(&db, "#Test", long_description) == -1);
    assert(chanserv_db_set_topic(&db, "#Test", long_topic,
                                  "Alice!alice@example", 1) == -1);
    assert(chanserv_db_set_topic(&db, "#Test", "valid", long_setter, 1) == -1);
    assert(chanserv_db_get(&db, long_name, &record) == -1);

    assert(chanserv_db_create(&db, "#Second", "Alice", "Second channel") == 0);
    assert(chanserv_db_create(&db, "#Overflow", "Alice", "Must be refused") != 0);
    assert(chanserv_db_get(&db, "#Overflow", &record) == 0);
    assert(chanserv_db_delete(&db, "#Second") == 0);
    generation = chanserv_db_pchannels_generation();

    assert(chanserv_db_get(&db, "#test", &record) == 1);
    assert(strcmp(record.name, "#Test") == 0);
    assert(strcmp(record.founder, "Alice") == 0);
    assert(strcmp(record.description, "Example channel") == 0);
    assert(record.enabled == 1);
    assert(record.mode_lock == 0U);
    assert(record.topic[0] == '\0');

    assert(chanserv_db_set_mode_lock(&db, "#test",
           CHANNEL_MODE_NO_EXTERNAL | CHANNEL_MODE_TOPIC_LOCK) == 0);
    assert(chanserv_db_set_topic(&db, "#test", "Persistent topic",
                                  "Alice!alice@example", 12345) == 0);
    assert(chanserv_db_pchannels_generation() == generation);
    assert(chanserv_db_get(&db, "#TEST", &record) == 1);
    assert((record.mode_lock & CHANNEL_MODE_NO_EXTERNAL) != 0U);
    assert((record.mode_lock & CHANNEL_MODE_TOPIC_LOCK) != 0U);
    assert(strcmp(record.topic, "Persistent topic") == 0);
    assert(strcmp(record.topic_setter, "Alice!alice@example") == 0);
    assert(record.topic_time == 12345);

    /* Externally edited/legacy rows must fail closed rather than being
     * silently truncated into different valid-looking IRC identities/state. */
    raw_set_channel_text(db.db, "founder", "#Test", long_founder);
    assert(chanserv_db_get(&db, "#Test", &record) == -1);
    raw_set_channel_text(db.db, "founder", "#Test", "Alice");
    assert(chanserv_db_get(&db, "#Test", &record) == 1);

    raw_set_channel_text(db.db, "description", "#Test", long_description);
    assert(chanserv_db_get(&db, "#Test", &record) == -1);
    raw_set_channel_text(db.db, "description", "#Test", "Example channel");
    assert(chanserv_db_get(&db, "#Test", &record) == 1);

    raw_set_channel_text(db.db, "topic", "#Test", long_topic);
    assert(chanserv_db_get(&db, "#Test", &record) == -1);
    raw_set_channel_text(db.db, "topic", "#Test", "Persistent topic");
    assert(chanserv_db_get(&db, "#Test", &record) == 1);

    raw_set_channel_text(db.db, "topic_setter", "#Test", long_setter);
    assert(chanserv_db_get(&db, "#Test", &record) == -1);
    raw_set_channel_text(db.db, "topic_setter", "#Test", "Alice!alice@example");
    assert(chanserv_db_get(&db, "#Test", &record) == 1);

    raw_set_channel_text(db.db, "name", "#Test", long_name);
    assert(chanserv_db_get(&db, long_name, &record) == -1);
    raw_set_channel_text(db.db, "name", long_name, "#Test");
    assert(chanserv_db_get(&db, "#Test", &record) == 1);

    assert(CHANSERV_ACCESS_VOICE == 1);
    assert(CHANSERV_ACCESS_HALFOP == 2);
    assert(CHANSERV_ACCESS_OP == 3);
    assert(CHANSERV_ACCESS_PROTECTED == 4);
    assert(CHANSERV_ACCESS_OWNER == 5);

    assert(chanserv_db_access_set(&db, "#TEST", long_account, CHANSERV_ACCESS_OP) == -1);
    assert(chanserv_db_access_set(&db, long_name, "Bob", CHANSERV_ACCESS_OP) == -1);
    assert(chanserv_db_access_set(&db, "#TEST", "Bob", CHANSERV_ACCESS_OP) == 0);
    assert(chanserv_db_access_get(&db, "#test", "bob", &access) == 1);
    assert(strcmp(access.account, "Bob") == 0);
    assert(access.level == CHANSERV_ACCESS_OP);

    raw_set_access_account(db.db, "#Test", "Bob", long_account);
    assert(chanserv_db_access_get(&db, "#Test", long_account, &access) == -1);
    raw_set_access_account(db.db, "#Test", long_account, "Bob");
    assert(chanserv_db_access_get(&db, "#Test", "Bob", &access) == 1);

    assert(chanserv_db_access_set(&db, "#TEST", "Carol",
                                  CHANSERV_ACCESS_PROTECTED) == 0);
    assert(chanserv_db_access_get(&db, "#test", "carol", &access) == 1);
    assert(access.level == CHANSERV_ACCESS_PROTECTED);

    assert(chanserv_db_access_set(&db, "#TEST", "Dave", CHANSERV_ACCESS_VOICE) != 0);
    assert(chanserv_db_access_get(&db, "#test", "Dave", &access) == 0);

    assert(chanserv_db_access_list(&db, "#test", list, sizeof(list)) == 0);
    assert(strstr(list, "Bob:3") != NULL);
    assert(strstr(list, "Carol:4") != NULL);
    {
        char tiny[8];
        assert(chanserv_db_access_list(&db, "#test", tiny, sizeof(tiny)) == -1);
    }

    /* Existing entries remain updateable even when the access list is full. */
    assert(chanserv_db_access_set(&db, "#test", "Bob", CHANSERV_ACCESS_VOICE) == 0);
    assert(chanserv_db_access_get(&db, "#TEST", "BOB", &access) == 1);
    assert(access.level == CHANSERV_ACCESS_VOICE);
    assert(chanserv_db_access_delete(&db, "#test", "bob") == 0);
    assert(chanserv_db_access_set(&db, "#TEST", "Dave", CHANSERV_ACCESS_VOICE) == 0);
    assert(chanserv_db_access_delete(&db, "#test", "Dave") == 0);
    assert(chanserv_db_access_delete(&db, "#test", "carol") == 0);
    assert(chanserv_db_access_get(&db, "#test", "Bob", &access) == 0);
    assert(chanserv_db_pchannels_generation() == generation);

    assert(chanserv_db_list_enabled(&db, list, sizeof(list)) == 0);
    assert(strstr(list, "#Test") != NULL);
    {
        char tiny[5];
        assert(chanserv_db_list_enabled(&db, tiny, sizeof(tiny)) == -1);
    }

    assert(chanserv_db_set_description(&db, "#TEST", "Changed") == 0);
    assert(chanserv_db_set_founder(&db, "#test", "Bob") == 0);
    assert(chanserv_db_pchannels_generation() == generation);
    assert(chanserv_db_set_enabled(&db, "#test", 0) == 0);
    assert(chanserv_db_pchannels_generation() != generation);
    generation = chanserv_db_pchannels_generation();
    assert(chanserv_db_get(&db, "#test", &record) == 1);
    assert(strcmp(record.founder, "Bob") == 0);
    assert(strcmp(record.description, "Changed") == 0);
    assert(record.enabled == 0);

    assert(chanserv_db_list_enabled(&db, list, sizeof(list)) == 0);
    assert(list[0] == '\0');

    assert(chanserv_db_delete(&db, "#test") == 0);
    assert(chanserv_db_pchannels_generation() != generation);
    assert(chanserv_db_get(&db, "#test", &record) == 0);

    /* SQLite persistence must use the same RFC1459 casemapping as hashes. */
    assert(chanserv_db_create(&db, "#[Fold]", "Alice", "RFC1459") == 0);
    assert(chanserv_db_get(&db, "#{fOLD}", &record) == 1);
    assert(strcmp(record.name, "#[Fold]") == 0);
    assert(chanserv_db_delete(&db, "#{fold}") == 0);

    chanserv_db_close(&db);

    /* Schema version persists, so a normal reopen does not need legacy scans. */
    assert(chanserv_db_open(&db, path) == 0);
    assert(pragma_int(db.db, "PRAGMA user_version") == 1);
    assert(pragma_int(db.db, "PRAGMA busy_timeout") == 250);
    chanserv_db_close(&db);

    unlink(path);
    puts("chanserv db tests passed");
    return 0;
}