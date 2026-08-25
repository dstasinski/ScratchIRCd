/**
 * @file ban_db.c
 * @brief SQLite persistence for KLINE and ZLINE records.
 */

#include "ban_db.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

/** Match one numeric address against an IPv4/IPv6 CIDR mask. */
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

static void record_from_stmt(sqlite3_stmt *stmt, BanRecord *record) {
    const unsigned char *text;
    memset(record, 0, sizeof(*record));
    record->type = (BanType)sqlite3_column_int(stmt, 0);
    text = sqlite3_column_text(stmt, 1);
    if (text != NULL) snprintf(record->mask, sizeof(record->mask), "%s", (const char *)text);
    text = sqlite3_column_text(stmt, 2);
    if (text != NULL) snprintf(record->reason, sizeof(record->reason), "%s", (const char *)text);
    text = sqlite3_column_text(stmt, 3);
    if (text != NULL) snprintf(record->set_by, sizeof(record->set_by), "%s", (const char *)text);
    record->created_at = sqlite3_column_int64(stmt, 4);
    record->expires_at = sqlite3_column_int64(stmt, 5);
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

int ban_db_purge_expired(BanDb *db) {
    if (db == NULL || db->handle == NULL) return -1;
    return sqlite3_exec(db->handle,
        "DELETE FROM bans WHERE expires_at > 0 AND expires_at <= unixepoch()",
        NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
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
    if (sqlite3_exec(db->handle, schema_sql, NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        ban_db_close(db);
        return -1;
    }
    if (ensure_expires_column(db) != 0 || ban_db_purge_expired(db) != 0) {
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
    if (db == NULL || db->handle == NULL || mask == NULL || *mask == '\0') return -1;
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
    if (db == NULL || db->handle == NULL || mask == NULL) return -1;
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
    if (db == NULL || db->handle == NULL || callback == NULL) return -1;
    if (ban_db_purge_expired(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        BanRecord record;
        record_from_stmt(stmt, &record);
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
    if (db == NULL || db->handle == NULL || record == NULL) return -1;
    if (ban_db_purge_expired(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        BanRecord candidate;
        int matched = 0;
        record_from_stmt(stmt, &candidate);
        if (type == BAN_TYPE_ZLINE && strchr(candidate.mask, '/') != NULL) {
            matched = (identity1 != NULL && cidr_match(candidate.mask, identity1)) ||
                      (identity2 != NULL && cidr_match(candidate.mask, identity2));
        } else if (type == BAN_TYPE_ZLINE && strchr(candidate.mask, '*') == NULL &&
                   strchr(candidate.mask, '?') == NULL) {
            matched = (identity1 != NULL && numeric_ip_equal(candidate.mask, identity1)) ||
                      (identity2 != NULL && numeric_ip_equal(candidate.mask, identity2));
        } else {
            matched = (identity1 != NULL && wildcard_match(candidate.mask, identity1)) ||
                      (identity2 != NULL && wildcard_match(candidate.mask, identity2));
        }
        if (matched) {
            *record = candidate;
            sqlite3_finalize(stmt);
            return 1;
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}
