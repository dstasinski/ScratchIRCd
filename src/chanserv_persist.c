/**
 * @file chanserv_persist.c
 * @brief SQLite persistence for ChanServ parameter modes and mask lists.
 *
 * Registered-channel metadata and account access live in chanserv_db.c. This
 * module stores mutable runtime state changed by MODE: +k, +l, +j, +L, +B,
 * and the +b/+e/+I lists. Mask kinds are stored as integers rather than IRC
 * mode letters so SQLite collation/case rules can never alter their meaning.
 */

#include "chanserv_persist.h"
#include "sqlite_policy.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/** Persistent numeric identifiers for the three channel mask lists. */
enum {
    CHANSERV_MASK_BAN = 1,
    CHANSERV_MASK_EXCEPTION = 2,
    CHANSERV_MASK_INVEX = 3
};

static sqlite3 *shared_db = NULL;
static char shared_path[IRCD_CONFIG_PATH_MAX + 1U];
static int shared_schema_ready = 0;

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

static int copy_stmt_text(sqlite3_stmt *stmt, int column,
                          char *destination, size_t destination_size) {
    const unsigned char *text;
    int bytes;
    if (stmt == NULL || destination == NULL || destination_size == 0U) return -1;
    text = sqlite3_column_text(stmt, column);
    bytes = sqlite3_column_bytes(stmt, column);
    if (text == NULL || bytes < 0 || (size_t)bytes >= destination_size ||
        memchr(text, '\0', (size_t)bytes) != NULL ||
        memchr(text, '\r', (size_t)bytes) != NULL ||
        memchr(text, '\n', (size_t)bytes) != NULL)
        return -1;
    memcpy(destination, text, (size_t)bytes);
    destination[bytes] = '\0';
    return 0;
}

static int text_fits(const char *text, size_t maximum) {
    size_t length;
    if (text == NULL) return 0;
    length = strnlen(text, maximum + 1U);
    return length <= maximum &&
           memchr(text, '\r', length) == NULL &&
           memchr(text, '\n', length) == NULL;
}

static void close_shared(void) {
    if (shared_db != NULL) sqlite3_close(shared_db);
    shared_db = NULL;
    shared_path[0] = '\0';
    shared_schema_ready = 0;
}

void chanserv_persist_reset(void) {
    close_shared();
}

static int open_shared(const char *path) {
    sqlite3 *db = NULL;
    if (path == NULL || *path == '\0' || strlen(path) >= sizeof(shared_path)) return -1;
    if (shared_db != NULL && strcmp(shared_path, path) == 0) return 0;

    close_shared();
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        if (db != NULL) sqlite3_close(db);
        return -1;
    }
    if (ircd_sqlite_apply_policy(db) != 0 ||
        sqlite3_create_collation(db, "IRCNOCASE", SQLITE_UTF8, NULL,
                                 irc_collation) != SQLITE_OK ||
        exec_sql(db, "PRAGMA foreign_keys=ON;") != 0) {
        sqlite3_close(db);
        return -1;
    }
    shared_db = db;
    (void)snprintf(shared_path, sizeof(shared_path), "%s", path);
    return 0;
}

/** Return non-zero when channel_masks.type is declared as INTEGER. */
static int mask_schema_is_numeric(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int numeric = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(channel_masks)",
                           -1, &stmt, NULL) != SQLITE_OK) return 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *type = (const char *)sqlite3_column_text(stmt, 2);
        if (name != NULL && strcmp(name, "type") == 0) {
            numeric = type != NULL && strcmp(type, "INTEGER") == 0;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return numeric;
}

static int ensure_schema(sqlite3 *db) {
    static const char runtime_schema[] =
        "CREATE TABLE IF NOT EXISTS channel_runtime ("
        "channel TEXT COLLATE IRCNOCASE PRIMARY KEY,"
        "channel_key TEXT NOT NULL DEFAULT '',"
        "user_limit INTEGER NOT NULL DEFAULT 0,"
        "join_count INTEGER NOT NULL DEFAULT 0,"
        "join_seconds INTEGER NOT NULL DEFAULT 0,"
        "limit_redirect TEXT NOT NULL DEFAULT '',"
        "ban_redirect TEXT NOT NULL DEFAULT '',"
        "FOREIGN KEY(channel) REFERENCES channels(name) ON DELETE CASCADE"
        ");";
    static const char mask_schema[] =
        "CREATE TABLE IF NOT EXISTS channel_masks ("
        "channel TEXT COLLATE IRCNOCASE NOT NULL,"
        "type INTEGER NOT NULL CHECK(type BETWEEN 1 AND 3),"
        "mask TEXT NOT NULL,"
        "protected_authorized INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(channel,type,mask),"
        "FOREIGN KEY(channel) REFERENCES channels(name) ON DELETE CASCADE"
        ");";
    sqlite3_stmt *stmt = NULL;
    int exists = 0;
    int rc = -1;

    if (exec_sql(db, runtime_schema) != 0) return -1;
    if (sqlite3_prepare_v2(db,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name='channel_masks'",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt); stmt = NULL;

    if (exists && !mask_schema_is_numeric(db)) {
        if (exec_sql(db, "DROP TABLE channel_masks;") != 0) return -1;
    }
    if (exec_sql(db, mask_schema) == 0) rc = 0;
    return rc;
}

/**
 * Ensure the runtime persistence schema exists once for the active database.
 * Early 0.20 development builds used TEXT mode letters in channel_masks.type;
 * that development-only table is rebuilt once if encountered.
 */
int chanserv_persist_init(const char *path) {
    if (open_shared(path) != 0) return -1;
    if (shared_schema_ready) return 0;
    if (ensure_schema(shared_db) != 0) return -1;
    shared_schema_ready = 1;
    return 0;
}

static sqlite3 *ready_db(const char *path) {
    return chanserv_persist_init(path) == 0 ? shared_db : NULL;
}

static int save_masks(sqlite3 *db, const char *channel,
                      const ChannelMaskEntry *list, int type) {
    sqlite3_stmt *stmt = NULL;
    const ChannelMaskEntry *entry;
    if (sqlite3_prepare_v2(db,
        "INSERT INTO channel_masks(channel,type,mask,protected_authorized) "
        "VALUES(?1,?2,?3,?4)", -1, &stmt, NULL) != SQLITE_OK) return -1;
    for (entry = list; entry != NULL; entry = entry->next) {
        if (!text_fits(entry->mask, IRC_CHANNEL_MASK_MAX)) {
            sqlite3_finalize(stmt);
            return -1;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_text(stmt, 1, channel, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, type);
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
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    int ok = -1;
    if (path == NULL || channel == NULL ||
        !text_fits(channel->name, IRC_CHANNEL_NAME_MAX) ||
        !text_fits(channel->key, IRC_CHANNEL_KEY_MAX) ||
        !text_fits(channel->limit_redirect, IRC_CHANNEL_NAME_MAX) ||
        !text_fits(channel->ban_redirect, IRC_CHANNEL_NAME_MAX)) return -1;
    db = ready_db(path);
    if (db == NULL) return -1;
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
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)channel->join_throttle_count);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)channel->join_throttle_seconds);
    sqlite3_bind_text(stmt, 6, channel->limit_redirect, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, channel->ban_redirect, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) goto rollback;
    sqlite3_finalize(stmt); stmt = NULL;

    if (sqlite3_prepare_v2(db, "DELETE FROM channel_masks WHERE channel=?1",
                           -1, &stmt, NULL) != SQLITE_OK) goto rollback;
    sqlite3_bind_text(stmt, 1, channel->name, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) goto rollback;
    sqlite3_finalize(stmt); stmt = NULL;

    if (save_masks(db, channel->name, channel->ban_list,
                   CHANSERV_MASK_BAN) != 0 ||
        save_masks(db, channel->name, channel->exception_list,
                   CHANSERV_MASK_EXCEPTION) != 0 ||
        save_masks(db, channel->name, channel->invite_exception_list,
                   CHANSERV_MASK_INVEX) != 0)
        goto rollback;

    if (exec_sql(db, "COMMIT;") != 0) goto done;
    ok = 0;
    goto done;

rollback:
    if (stmt != NULL) { sqlite3_finalize(stmt); stmt = NULL; }
    (void)exec_sql(db, "ROLLBACK;");
done:
    if (stmt != NULL) sqlite3_finalize(stmt);
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
        int type = sqlite3_column_int(stmt, 0);
        int authorized = sqlite3_column_int(stmt, 2);
        char mask[IRC_CHANNEL_MASK_MAX + 1U];
        ChannelMaskEntry **list;
        if (copy_stmt_text(stmt, 1, mask, sizeof(mask)) != 0 || mask[0] == '\0' ||
            (authorized != 0 && authorized != 1)) {
            sqlite3_finalize(stmt);
            return -1;
        }
        switch (type) {
            case CHANSERV_MASK_BAN: list = &channel->ban_list; break;
            case CHANSERV_MASK_EXCEPTION: list = &channel->exception_list; break;
            case CHANSERV_MASK_INVEX: list = &channel->invite_exception_list; break;
            default:
                sqlite3_finalize(stmt);
                return -1;
        }
        if (channel_mask_add_authorized(list, mask, authorized) != 0) {
            sqlite3_finalize(stmt);
            return -1;
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_persist_restore(const char *path, Channel *channel) {
    sqlite3 *db;
    sqlite3_stmt *stmt = NULL;
    int rc;
    int result = -1;
    if (path == NULL || channel == NULL ||
        !text_fits(channel->name, IRC_CHANNEL_NAME_MAX)) return -1;
    db = ready_db(path);
    if (db == NULL) return -1;

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
        sqlite3_int64 user_limit = sqlite3_column_int64(stmt, 1);
        sqlite3_int64 join_count = sqlite3_column_int64(stmt, 2);
        sqlite3_int64 join_seconds = sqlite3_column_int64(stmt, 3);
        if (copy_stmt_text(stmt, 0, channel->key, sizeof(channel->key)) != 0 ||
            copy_stmt_text(stmt, 4, channel->limit_redirect,
                           sizeof(channel->limit_redirect)) != 0 ||
            copy_stmt_text(stmt, 5, channel->ban_redirect,
                           sizeof(channel->ban_redirect)) != 0 ||
            user_limit < 0 || (uint64_t)user_limit > (uint64_t)SIZE_MAX ||
            join_count < 0 || (uint64_t)join_count > (uint64_t)UINT_MAX ||
            join_seconds < 0 || (uint64_t)join_seconds > (uint64_t)UINT_MAX)
            goto done;
        channel->user_limit = (size_t)user_limit;
        channel->join_throttle_count = (unsigned int)join_count;
        channel->join_throttle_seconds = (unsigned int)join_seconds;
    } else if (rc != SQLITE_DONE) goto done;
    sqlite3_finalize(stmt); stmt = NULL;

    if (restore_masks(db, channel) != 0) goto done;
    result = 0;

done:
    if (stmt != NULL) sqlite3_finalize(stmt);
    if (result != 0) {
        channel->key[0] = '\0';
        channel->user_limit = 0U;
        channel->join_throttle_count = 0U;
        channel->join_throttle_seconds = 0U;
        channel->limit_redirect[0] = '\0';
        channel->ban_redirect[0] = '\0';
        channel_mask_clear(&channel->ban_list);
        channel_mask_clear(&channel->exception_list);
        channel_mask_clear(&channel->invite_exception_list);
    }
    return result;
}
