/**
 * @file chanserv_persist.c
 * @brief SQLite persistence for ChanServ parameter modes and mask lists.
 *
 * Registered-channel metadata and account access live in chanserv_db.c. This
 * module stores the mutable runtime state that ordinary MODE commands can
 * change: +k, +l, +j, +L, +B, and the +b/+e/+I lists. All data lives in the
 * same chanserv.db and is restored when a persistent channel is recreated.
 */

#include "chanserv_persist.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static unsigned char irc_fold(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return (unsigned char)(ch + ('a' - 'A'));
    switch (ch) {
        case '{': return '[';
        case '}': return ']';
        case '|': return '\\';
        case '~': return '^';
        default: return ch;
    }
}

static int irc_collation(void *context, int left_len, const void *left_data,
                         int right_len, const void *right_data) {
    const unsigned char *left = left_data;
    const unsigned char *right = right_data;
    int length = left_len < right_len ? left_len : right_len;
    int i;
    (void)context;
    for (i = 0; i < length; ++i) {
        unsigned char a = irc_fold(left[i]);
        unsigned char b = irc_fold(right[i]);
        if (a < b) return -1;
        if (a > b) return 1;
    }
    return left_len < right_len ? -1 : left_len > right_len ? 1 : 0;
}

static int exec_sql(sqlite3 *db, const char *sql) {
    char *error = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &error);
    if (rc != SQLITE_OK) {
        if (error != NULL) fprintf(stderr, "ChanServ persistence: %s\n", error);
        sqlite3_free(error);
        return -1;
    }
    return 0;
}

static int open_db(sqlite3 **out, const char *path) {
    sqlite3 *db = NULL;
    if (out == NULL || path == NULL) return -1;
    *out = NULL;
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        if (db != NULL) sqlite3_close(db);
        return -1;
    }
    if (sqlite3_create_collation(db, "IRCNOCASE", SQLITE_UTF8, NULL,
                                 irc_collation) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }
    sqlite3_busy_timeout(db, 2000);
    if (exec_sql(db, "PRAGMA foreign_keys=ON;") != 0) {
        sqlite3_close(db);
        return -1;
    }
    *out = db;
    return 0;
}

int chanserv_persist_init(const char *path) {
    static const char schema[] =
        "CREATE TABLE IF NOT EXISTS channel_runtime ("
        "channel TEXT COLLATE IRCNOCASE PRIMARY KEY,"
        "channel_key TEXT NOT NULL DEFAULT '',"
        "user_limit INTEGER NOT NULL DEFAULT 0,"
        "join_count INTEGER NOT NULL DEFAULT 0,"
        "join_seconds INTEGER NOT NULL DEFAULT 0,"
        "limit_redirect TEXT NOT NULL DEFAULT '',"
        "ban_redirect TEXT NOT NULL DEFAULT '',"
        "FOREIGN KEY(channel) REFERENCES channels(name) ON DELETE CASCADE"
        ");"
        "CREATE TABLE IF NOT EXISTS channel_masks ("
        "channel TEXT COLLATE IRCNOCASE NOT NULL,"
        "type TEXT NOT NULL CHECK(type IN ('b','e','I')) ,"
        "mask TEXT NOT NULL,"
        "protected_authorized INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(channel,type,mask),"
        "FOREIGN KEY(channel) REFERENCES channels(name) ON DELETE CASCADE"
        ");";
    sqlite3 *db = NULL;
    int rc;
    if (open_db(&db, path) != 0) return -1;
    rc = exec_sql(db, schema);
    sqlite3_close(db);
    return rc;
}

static int save_masks(sqlite3 *db, const char *channel,
                      const ChannelMaskEntry *list, const char *type) {
    sqlite3_stmt *stmt = NULL;
    const ChannelMaskEntry *entry;
    if (sqlite3_prepare_v2(db,
        "INSERT INTO channel_masks(channel,type,mask,protected_authorized) "
        "VALUES(?1,?2,?3,?4)", -1, &stmt, NULL) != SQLITE_OK) return -1;
    for (entry = list; entry != NULL; entry = entry->next) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_text(stmt, 1, channel, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, entry->mask, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, entry->protected_authorized ? 1 : 0);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return -1;
        }
    }
    sqlite3_finalize(stmt);
    return 0;
}

int chanserv_persist_save(const char *path, const Channel *channel) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int ok = -1;
    if (path == NULL || channel == NULL) return -1;
    if (chanserv_persist_init(path) != 0 || open_db(&db, path) != 0) return -1;
    if (exec_sql(db, "BEGIN IMMEDIATE;") != 0) goto done;
    if (sqlite3_prepare_v2(db,
        "INSERT INTO channel_runtime(channel,channel_key,user_limit,join_count,join_seconds,limit_redirect,ban_redirect) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7) "
        "ON CONFLICT(channel) DO UPDATE SET "
        "channel_key=excluded.channel_key,user_limit=excluded.user_limit,"
        "join_count=excluded.join_count,join_seconds=excluded.join_seconds,"
        "limit_redirect=excluded.limit_redirect,ban_redirect=excluded.ban_redirect",
        -1, &stmt, NULL) != SQLITE_OK) goto rollback;
    sqlite3_bind_text(stmt, 1, channel->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, channel->key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)channel->user_limit);
    sqlite3_bind_int(stmt, 4, (int)channel->join_throttle_count);
    sqlite3_bind_int(stmt, 5, (int)channel->join_throttle_seconds);
    sqlite3_bind_text(stmt, 6, channel->limit_redirect, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, channel->ban_redirect, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) goto rollback;
    sqlite3_finalize(stmt); stmt = NULL;
    if (sqlite3_prepare_v2(db, "DELETE FROM channel_masks WHERE channel=?1",
                           -1, &stmt, NULL) != SQLITE_OK) goto rollback;
    sqlite3_bind_text(stmt, 1, channel->name, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) goto rollback;
    sqlite3_finalize(stmt); stmt = NULL;
    if (save_masks(db, channel->name, channel->ban_list, "b") != 0 ||
        save_masks(db, channel->name, channel->exception_list, "e") != 0 ||
        save_masks(db, channel->name, channel->invite_exception_list, "I") != 0)
        goto rollback;
    if (exec_sql(db, "COMMIT;") != 0) goto done;
    ok = 0;
    goto done;
rollback:
    if (stmt != NULL) { sqlite3_finalize(stmt); stmt = NULL; }
    (void)exec_sql(db, "ROLLBACK;");
done:
    if (stmt != NULL) sqlite3_finalize(stmt);
    if (db != NULL) sqlite3_close(db);
    return ok;
}

static int restore_masks(sqlite3 *db, Channel *channel) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (sqlite3_prepare_v2(db,
        "SELECT type,mask,protected_authorized FROM channel_masks "
        "WHERE channel=?1 ORDER BY rowid", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, channel->name, -1, SQLITE_TRANSIENT);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *type = (const char *)sqlite3_column_text(stmt, 0);
        const char *mask = (const char *)sqlite3_column_text(stmt, 1);
        int authorized = sqlite3_column_int(stmt, 2);
        ChannelMaskEntry **list;
        if (type == NULL || mask == NULL) continue;
        if (strcmp(type, "b") == 0) list = &channel->ban_list;
        else if (strcmp(type, "e") == 0) list = &channel->exception_list;
        else if (strcmp(type, "I") == 0) list = &channel->invite_exception_list;
        else continue;
        if (channel_mask_add_authorized(list, mask, authorized) != 0) {
            sqlite3_finalize(stmt);
            return -1;
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_persist_restore(const char *path, Channel *channel) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int result = -1;
    if (path == NULL || channel == NULL) return -1;
    if (chanserv_persist_init(path) != 0 || open_db(&db, path) != 0) return -1;

    channel->key[0] = '\0';
    channel->user_limit = 0U;
    channel->join_throttle_count = 0U;
    channel->join_throttle_seconds = 0U;
    channel->limit_redirect[0] = '\0';
    channel->ban_redirect[0] = '\0';
    channel_mask_clear(&channel->ban_list);
    channel_mask_clear(&channel->exception_list);
    channel_mask_clear(&channel->invite_exception_list);

    if (sqlite3_prepare_v2(db,
        "SELECT channel_key,user_limit,join_count,join_seconds,limit_redirect,ban_redirect "
        "FROM channel_runtime WHERE channel=?1", -1, &stmt, NULL) != SQLITE_OK) goto done;
    sqlite3_bind_text(stmt, 1, channel->name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const char *key = (const char *)sqlite3_column_text(stmt, 0);
        const char *limit_redirect = (const char *)sqlite3_column_text(stmt, 4);
        const char *ban_redirect = (const char *)sqlite3_column_text(stmt, 5);
        (void)snprintf(channel->key, sizeof(channel->key), "%s", key != NULL ? key : "");
        channel->user_limit = (size_t)sqlite3_column_int64(stmt, 1);
        channel->join_throttle_count = (unsigned int)sqlite3_column_int(stmt, 2);
        channel->join_throttle_seconds = (unsigned int)sqlite3_column_int(stmt, 3);
        (void)snprintf(channel->limit_redirect, sizeof(channel->limit_redirect), "%s",
                       limit_redirect != NULL ? limit_redirect : "");
        (void)snprintf(channel->ban_redirect, sizeof(channel->ban_redirect), "%s",
                       ban_redirect != NULL ? ban_redirect : "");
    } else if (rc != SQLITE_DONE) goto done;
    sqlite3_finalize(stmt); stmt = NULL;
    if (restore_masks(db, channel) != 0) goto done;
    result = 0;
done:
    if (stmt != NULL) sqlite3_finalize(stmt);
    if (db != NULL) sqlite3_close(db);
    return result;
}
