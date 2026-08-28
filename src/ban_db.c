/**
 * @file ban_db.c
 * @brief SQLite persistence for KLINE and ZLINE records.
 */

#include "ban_db.h"
#include "sqlite_policy.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define BAN_DB_SCHEMA_VERSION 1
#define BAN_PURGE_INTERVAL_SECONDS 300

static time_t ban_last_purge;
static char ban_last_purge_path[IRCD_CONFIG_PATH_MAX + 1U];

static const char *schema_sql =
    "CREATE TABLE IF NOT EXISTS bans ("
    "type INTEGER NOT NULL,"
    "mask TEXT COLLATE NOCASE NOT NULL,"
    "reason TEXT NOT NULL DEFAULT '',"
    "set_by TEXT NOT NULL DEFAULT '',"
    "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
    "expires_at INTEGER NOT NULL DEFAULT 0,"
    "PRIMARY KEY(type,mask)"
    ");";

static int valid_type(BanType type) {
    return type == BAN_TYPE_KLINE || type == BAN_TYPE_ZLINE;
}

static int has_line_break(const char *text) {
    return text != NULL && (strchr(text, '\r') != NULL || strchr(text, '\n') != NULL);
}

static int ensure_parent_directory(const char *path) {
    char parent[IRCD_CONFIG_PATH_MAX + 1U];
    char *slash;
    size_t length;
    if (path == NULL) return -1;
    length = strlen(path);
    if (length == 0U || length >= sizeof(parent)) return -1;
    (void)snprintf(parent, sizeof(parent), "%s", path);
    slash = strrchr(parent, '/');
    if (slash == NULL || slash == parent) return 0;
    *slash = '\0';
    return mkdir(parent, 0750) == 0 || errno == EEXIST ? 0 : -1;
}

static int wildcard_match(const char *pattern, const char *text) {
    const char *star = NULL;
    const char *retry = NULL;
    if (pattern == NULL || text == NULL) return 0;
    while (*text != '\0') {
        if (*pattern == '*') {
            star = pattern++;
            retry = text;
        } else if (*pattern == '?' ||
                   tolower((unsigned char)*pattern) == tolower((unsigned char)*text)) {
            ++pattern;
            ++text;
        } else if (star != NULL) {
            pattern = star + 1;
            text = ++retry;
        } else {
            return 0;
        }
    }
    while (*pattern == '*') ++pattern;
    return *pattern == '\0';
}

static int numeric_ip_equal(const char *left, const char *right) {
    struct in_addr left4, right4;
    struct in6_addr left6, right6;
    if (left == NULL || right == NULL) return 0;
    if (inet_pton(AF_INET, left, &left4) == 1 && inet_pton(AF_INET, right, &right4) == 1)
        return memcmp(&left4, &right4, sizeof(left4)) == 0;
    if (inet_pton(AF_INET6, left, &left6) == 1 && inet_pton(AF_INET6, right, &right6) == 1)
        return memcmp(&left6, &right6, sizeof(left6)) == 0;
    return 0;
}

static int cidr_match(const char *cidr, const char *address) {
    char network_text[IRC_IP_MAX + 1U];
    const char *slash;
    char *end = NULL;
    unsigned long prefix;
    unsigned char network[16];
    unsigned char candidate[16];
    unsigned int max_prefix;
    int family;
    size_t whole_bytes;
    unsigned int remainder;

    if (cidr == NULL || address == NULL) return 0;
    slash = strchr(cidr, '/');
    if (slash == NULL || slash == cidr || strchr(slash + 1, '/') != NULL) return 0;
    if ((size_t)(slash - cidr) >= sizeof(network_text)) return 0;
    memcpy(network_text, cidr, (size_t)(slash - cidr));
    network_text[slash - cidr] = '\0';

    errno = 0;
    prefix = strtoul(slash + 1, &end, 10);
    if (errno != 0 || end == slash + 1 || *end != '\0') return 0;

    if (inet_pton(AF_INET, network_text, network) == 1) {
        family = AF_INET;
        max_prefix = 32U;
    } else if (inet_pton(AF_INET6, network_text, network) == 1) {
        family = AF_INET6;
        max_prefix = 128U;
    } else {
        return 0;
    }
    if (prefix > max_prefix || inet_pton(family, address, candidate) != 1) return 0;

    whole_bytes = (size_t)(prefix / 8UL);
    remainder = (unsigned int)(prefix % 8UL);
    if (whole_bytes > 0U && memcmp(network, candidate, whole_bytes) != 0) return 0;
    if (remainder != 0U) {
        unsigned char mask = (unsigned char)(0xFFU << (8U - remainder));
        if ((network[whole_bytes] & mask) != (candidate[whole_bytes] & mask)) return 0;
    }
    return 1;
}

int ban_record_matches(const BanRecord *record, const char *identity1,
                       const char *identity2) {
    if (record == NULL) return 0;
    if (record->type == BAN_TYPE_ZLINE && strchr(record->mask, '/') != NULL) {
        return (identity1 != NULL && cidr_match(record->mask, identity1)) ||
               (identity2 != NULL && cidr_match(record->mask, identity2));
    }
    if (record->type == BAN_TYPE_ZLINE && strchr(record->mask, '*') == NULL &&
        strchr(record->mask, '?') == NULL) {
        return (identity1 != NULL && numeric_ip_equal(record->mask, identity1)) ||
               (identity2 != NULL && numeric_ip_equal(record->mask, identity2));
    }
    return (identity1 != NULL && wildcard_match(record->mask, identity1)) ||
           (identity2 != NULL && wildcard_match(record->mask, identity2));
}

static int copy_stmt_text(sqlite3_stmt *stmt, int column, char *dest, size_t size) {
    const unsigned char *value;
    int bytes;
    if (stmt == NULL || dest == NULL || size == 0U) return -1;
    value = sqlite3_column_text(stmt, column);
    bytes = sqlite3_column_bytes(stmt, column);
    if (value == NULL || bytes < 0 || (size_t)bytes >= size ||
        memchr(value, '\0', (size_t)bytes) != NULL ||
        memchr(value, '\r', (size_t)bytes) != NULL ||
        memchr(value, '\n', (size_t)bytes) != NULL)
        return -1;
    memcpy(dest, value, (size_t)bytes);
    dest[bytes] = '\0';
    return 0;
}

static int record_from_stmt(sqlite3_stmt *stmt, BanRecord *record) {
    BanType type;
    if (stmt == NULL || record == NULL) return -1;
    memset(record, 0, sizeof(*record));
    type = (BanType)sqlite3_column_int(stmt, 0);
    if (!valid_type(type) ||
        copy_stmt_text(stmt, 1, record->mask, sizeof(record->mask)) != 0 ||
        record->mask[0] == '\0' ||
        copy_stmt_text(stmt, 2, record->reason, sizeof(record->reason)) != 0 ||
        copy_stmt_text(stmt, 3, record->set_by, sizeof(record->set_by)) != 0)
        return -1;
    record->type = type;
    record->created_at = sqlite3_column_int64(stmt, 4);
    record->expires_at = sqlite3_column_int64(stmt, 5);
    return 0;
}

static int ensure_expires_column(BanDb *db) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    int found = 0;
    if (sqlite3_prepare_v2(db->handle, "PRAGMA table_info(bans)", -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        if (name != NULL && strcmp((const char *)name, "expires_at") == 0) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    if (found) return 0;
    return sqlite3_exec(db->handle,
        "ALTER TABLE bans ADD COLUMN expires_at INTEGER NOT NULL DEFAULT 0",
        NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static int schema_version(sqlite3 *db, int *version) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || version == NULL) return -1;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, NULL) != SQLITE_OK) return -1;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) *version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 0 : -1;
}

static int migrate_schema(BanDb *db) {
    int version = 0;
    if (schema_version(db->handle, &version) != 0) return -1;
    if (version >= BAN_DB_SCHEMA_VERSION) return 0;
    if (ensure_expires_column(db) != 0) return -1;
    return sqlite3_exec(db->handle, "PRAGMA user_version=1", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

int ban_db_purge_expired(BanDb *db) {
    if (db == NULL || db->handle == NULL) return -1;
    return sqlite3_exec(db->handle,
        "DELETE FROM bans WHERE expires_at > 0 AND expires_at <= unixepoch()",
        NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static int purge_expired_due(BanDb *db, const char *path) {
    time_t now;
    if (db == NULL || db->handle == NULL || path == NULL) return -1;
    now = time(NULL);
    if (ban_last_purge != (time_t)0 && now >= ban_last_purge &&
        (now - ban_last_purge) < BAN_PURGE_INTERVAL_SECONDS &&
        strcmp(ban_last_purge_path, path) == 0)
        return 0;
    if (ban_db_purge_expired(db) != 0) return -1;
    ban_last_purge = now;
    (void)snprintf(ban_last_purge_path, sizeof(ban_last_purge_path), "%s", path);
    return 0;
}

void ban_db_reset_runtime_state(void) {
    ban_last_purge = (time_t)0;
    ban_last_purge_path[0] = '\0';
}

int ban_db_open(BanDb *db, const char *path) {
    char *error = NULL;
    if (db == NULL || path == NULL || *path == '\0') return -1;
    memset(db, 0, sizeof(*db));
    if (ensure_parent_directory(path) != 0) return -1;
    if (sqlite3_open(path, &db->handle) != SQLITE_OK) {
        ban_db_close(db);
        return -1;
    }
    if (ircd_sqlite_apply_policy(db->handle) != 0) {
        ban_db_close(db);
        return -1;
    }
    if (sqlite3_exec(db->handle, schema_sql, NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        ban_db_close(db);
        return -1;
    }
    if (migrate_schema(db) != 0 || purge_expired_due(db, path) != 0) {
        ban_db_close(db);
        return -1;
    }
    return 0;
}

void ban_db_close(BanDb *db) {
    if (db != NULL && db->handle != NULL) {
        sqlite3_close(db->handle);
        db->handle = NULL;
    }
}

static int ban_db_add_internal(BanDb *db, BanType type, const char *mask,
                               const char *reason, const char *set_by,
                               unsigned int duration_seconds) {
    static const char sql[] =
        "INSERT OR REPLACE INTO bans(type,mask,reason,set_by,created_at,expires_at) "
        "VALUES(?1,?2,?3,?4,unixepoch(),CASE WHEN ?5=0 THEN 0 ELSE unixepoch()+?5 END)";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || !valid_type(type) ||
        mask == NULL || *mask == '\0' || strlen(mask) > IRC_CHANNEL_MASK_MAX ||
        has_line_break(mask) ||
        (reason != NULL && (strlen(reason) > IRC_QUIT_REASON_MAX || has_line_break(reason))) ||
        (set_by != NULL && (strlen(set_by) > IRCD_OPER_NAME_MAX || has_line_break(set_by))))
        return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    sqlite3_bind_text(stmt, 2, mask, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, reason != NULL ? reason : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, set_by != NULL ? set_by : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)duration_seconds);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int ban_db_add(BanDb *db, BanType type, const char *mask,
               const char *reason, const char *set_by) {
    return ban_db_add_internal(db, type, mask, reason, set_by, 0U);
}

int ban_db_add_timed(BanDb *db, BanType type, const char *mask,
                     const char *reason, const char *set_by,
                     unsigned int duration_seconds) {
    if (duration_seconds == 0U) return -1;
    return ban_db_add_internal(db, type, mask, reason, set_by, duration_seconds);
}

int ban_db_delete(BanDb *db, BanType type, const char *mask) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || !valid_type(type) ||
        mask == NULL || *mask == '\0' || strlen(mask) > IRC_CHANNEL_MASK_MAX ||
        has_line_break(mask))
        return -1;
    if (sqlite3_prepare_v2(db->handle,
            "DELETE FROM bans WHERE type=?1 AND mask=?2", -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    sqlite3_bind_text(stmt, 2, mask, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

int ban_db_list(BanDb *db, BanType type, BanDbListCallback callback, void *context) {
    static const char sql[] =
        "SELECT type,mask,reason,set_by,created_at,expires_at FROM bans "
        "WHERE type=?1 AND (expires_at=0 OR expires_at>unixepoch()) "
        "ORDER BY created_at,mask COLLATE NOCASE";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || !valid_type(type) || callback == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        BanRecord record;
        if (record_from_stmt(stmt, &record) != 0 || record.type != type) {
            sqlite3_finalize(stmt);
            return -1;
        }
        if (callback(&record, context) != 0) {
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int ban_db_match(BanDb *db, BanType type, const char *identity1,
                 const char *identity2, BanRecord *record) {
    static const char sql[] =
        "SELECT type,mask,reason,set_by,created_at,expires_at FROM bans "
        "WHERE type=?1 AND (expires_at=0 OR expires_at>unixepoch())";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || !valid_type(type) || record == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        BanRecord candidate;
        if (record_from_stmt(stmt, &candidate) != 0 || candidate.type != type) {
            sqlite3_finalize(stmt);
            return -1;
        }
        if (ban_record_matches(&candidate, identity1, identity2)) {
            *record = candidate;
            sqlite3_finalize(stmt);
            return 1;
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}
