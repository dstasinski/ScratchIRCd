/**
 * @file geoban_db.c
 * @brief Persistent COUNTRY/REGION/ASN/ORG GeoIP policy bans.
 */

#include "geoban_db.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *schema_sql =
    "CREATE TABLE IF NOT EXISTS geo_bans ("
    "type INTEGER NOT NULL CHECK(type BETWEEN 1 AND 4),"
    "value TEXT COLLATE NOCASE NOT NULL,"
    "reason TEXT NOT NULL DEFAULT '',"
    "set_by TEXT NOT NULL DEFAULT '',"
    "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
    "expires_at INTEGER NOT NULL DEFAULT 0,"
    "PRIMARY KEY(type,value)"
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

static int purge_expired(GeoBanDb *db) {
    char *error = NULL;
    if (sqlite3_exec(db->handle,
        "DELETE FROM geo_bans WHERE expires_at<>0 AND expires_at<=unixepoch()",
        NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        return -1;
    }
    return 0;
}

static void record_from_stmt(sqlite3_stmt *stmt, GeoBanRecord *record) {
    const unsigned char *text;
    memset(record, 0, sizeof(*record));
    record->type = (GeoBanType)sqlite3_column_int(stmt, 0);
    text = sqlite3_column_text(stmt, 1);
    if (text != NULL) (void)snprintf(record->value, sizeof(record->value), "%s", text);
    text = sqlite3_column_text(stmt, 2);
    if (text != NULL) (void)snprintf(record->reason, sizeof(record->reason), "%s", text);
    text = sqlite3_column_text(stmt, 3);
    if (text != NULL) (void)snprintf(record->set_by, sizeof(record->set_by), "%s", text);
    record->created_at = sqlite3_column_int64(stmt, 4);
    record->expires_at = sqlite3_column_int64(stmt, 5);
}

const char *geoban_type_name(GeoBanType type) {
    switch (type) {
        case GEOBAN_COUNTRY: return "COUNTRY";
        case GEOBAN_REGION: return "REGION";
        case GEOBAN_ASN: return "ASN";
        case GEOBAN_ORG: return "ORG";
        default: return "UNKNOWN";
    }
}

int geoban_type_parse(const char *text, GeoBanType *type) {
    if (text == NULL || type == NULL) return -1;
    if (strcasecmp(text, "COUNTRY") == 0) *type = GEOBAN_COUNTRY;
    else if (strcasecmp(text, "REGION") == 0) *type = GEOBAN_REGION;
    else if (strcasecmp(text, "ASN") == 0) *type = GEOBAN_ASN;
    else if (strcasecmp(text, "ORG") == 0) *type = GEOBAN_ORG;
    else return -1;
    return 0;
}

int geoban_normalize_value(GeoBanType type, const char *input,
                           char *output, size_t output_size) {
    size_t i, n;
    char *end = NULL;
    unsigned long asn;
    const char *number;
    if (input == NULL || output == NULL || output_size == 0U || *input == '\0') return -1;
    if (type == GEOBAN_COUNTRY || type == GEOBAN_REGION) {
        n = strlen(input);
        if (n < 1U || n >= output_size) return -1;
        for (i = 0U; i < n; ++i) output[i] = (char)toupper((unsigned char)input[i]);
        output[n] = '\0';
        if (type == GEOBAN_COUNTRY && n != 2U) return -1;
        return 0;
    }
    if (type == GEOBAN_ASN) {
        number = input;
        if ((input[0] == 'A' || input[0] == 'a') &&
            (input[1] == 'S' || input[1] == 's')) number += 2;
        if (*number == '\0') return -1;
        errno = 0;
        asn = strtoul(number, &end, 10);
        if (errno != 0 || end == number || *end != '\0' || asn > UINT32_MAX) return -1;
        return snprintf(output, output_size, "%lu", asn) > 0 ? 0 : -1;
    }
    if (type == GEOBAN_ORG) {
        n = strlen(input);
        if (n >= output_size) return -1;
        (void)snprintf(output, output_size, "%s", input);
        return 0;
    }
    return -1;
}

int geoban_duration_parse(const char *text, unsigned int *seconds) {
    unsigned long long number = 0ULL, multiplier = 1ULL, total;
    char *end = NULL;
    if (text == NULL || seconds == NULL || *text == '\0') return -1;
    if (strcasecmp(text, "permanent") == 0 || strcasecmp(text, "perm") == 0 ||
        strcasecmp(text, "forever") == 0 || strcmp(text, "0") == 0) {
        *seconds = 0U;
        return 0;
    }
    errno = 0;
    number = strtoull(text, &end, 10);
    if (errno != 0 || end == text || number == 0ULL) return -1;
    if (end[0] != '\0' && end[1] != '\0') return -1;
    if (end[0] != '\0') {
        switch (tolower((unsigned char)end[0])) {
            case 's': multiplier = 1ULL; break;
            case 'm': multiplier = 60ULL; break;
            case 'h': multiplier = 3600ULL; break;
            case 'd': multiplier = 86400ULL; break;
            case 'w': multiplier = 604800ULL; break;
            default: return -1;
        }
    }
    total = number * multiplier;
    if (total > IRCD_BAN_DURATION_HARD_MAX_SECONDS || total > UINT32_MAX) return -1;
    *seconds = (unsigned int)total;
    return 0;
}

/* Case-insensitive Tcl-style glob matcher supporting *, ?, [] and backslash. */
static int class_match(const char **pattern, unsigned char ch) {
    const char *p = *pattern;
    int negate = 0, matched = 0;
    unsigned char c, start, end;
    if (*p == '!' || *p == '^') { negate = 1; ++p; }
    while (*p != '\0' && *p != ']') {
        if (*p == '\\' && p[1] != '\0') ++p;
        start = (unsigned char)tolower((unsigned char)*p++);
        if (*p == '-' && p[1] != '\0' && p[1] != ']') {
            ++p;
            if (*p == '\\' && p[1] != '\0') ++p;
            end = (unsigned char)tolower((unsigned char)*p++);
            c = (unsigned char)tolower(ch);
            if (c >= start && c <= end) matched = 1;
        } else if ((unsigned char)tolower(ch) == start) {
            matched = 1;
        }
    }
    if (*p != ']') return -1;
    *pattern = p + 1;
    return negate ? !matched : matched;
}

static int glob_match_ci(const char *pattern, const char *text) {
    while (*pattern != '\0') {
        if (*pattern == '*') {
            while (*pattern == '*') ++pattern;
            if (*pattern == '\0') return 1;
            while (*text != '\0') {
                if (glob_match_ci(pattern, text)) return 1;
                ++text;
            }
            return glob_match_ci(pattern, text);
        }
        if (*text == '\0') return 0;
        if (*pattern == '?') { ++pattern; ++text; continue; }
        if (*pattern == '[') {
            int rc;
            ++pattern;
            rc = class_match(&pattern, (unsigned char)*text);
            if (rc <= 0) return 0;
            ++text;
            continue;
        }
        if (*pattern == '\\' && pattern[1] != '\0') ++pattern;
        if (tolower((unsigned char)*pattern) != tolower((unsigned char)*text)) return 0;
        ++pattern;
        ++text;
    }
    return *text == '\0';
}

int geoban_db_open(GeoBanDb *db, const char *path) {
    char *error = NULL;
    if (db == NULL || path == NULL || *path == '\0') return -1;
    memset(db, 0, sizeof(*db));
    if (ensure_parent_directory(path) != 0) return -1;
    if (sqlite3_open(path, &db->handle) != SQLITE_OK) {
        geoban_db_close(db);
        return -1;
    }
    if (sqlite3_exec(db->handle, schema_sql, NULL, NULL, &error) != SQLITE_OK) {
        sqlite3_free(error);
        geoban_db_close(db);
        return -1;
    }
    if (purge_expired(db) != 0) {
        geoban_db_close(db);
        return -1;
    }
    return 0;
}

void geoban_db_close(GeoBanDb *db) {
    if (db != NULL && db->handle != NULL) {
        sqlite3_close(db->handle);
        db->handle = NULL;
    }
}

int geoban_db_add(GeoBanDb *db, GeoBanType type, const char *value,
                  const char *reason, const char *set_by,
                  unsigned int duration_seconds) {
    static const char sql[] =
        "INSERT OR REPLACE INTO geo_bans(type,value,reason,set_by,created_at,expires_at) "
        "VALUES(?1,?2,?3,?4,unixepoch(),CASE WHEN ?5=0 THEN 0 ELSE unixepoch()+?5 END)";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || value == NULL || *value == '\0') return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, reason != NULL ? reason : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, set_by != NULL ? set_by : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)duration_seconds);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int geoban_db_delete(GeoBanDb *db, GeoBanType type, const char *value) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || value == NULL) return -1;
    if (sqlite3_prepare_v2(db->handle,
        "DELETE FROM geo_bans WHERE type=?1 AND value=?2", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, (int)type);
    sqlite3_bind_text(stmt, 2, value, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE && sqlite3_changes(db->handle) > 0 ? 0 : -1;
}

int geoban_db_list(GeoBanDb *db, GeoBanListCallback callback, void *context) {
    static const char sql[] =
        "SELECT type,value,reason,set_by,created_at,expires_at FROM geo_bans "
        "WHERE expires_at=0 OR expires_at>unixepoch() ORDER BY type,value COLLATE NOCASE";
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->handle == NULL || callback == NULL) return -1;
    if (purge_expired(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        GeoBanRecord record;
        record_from_stmt(stmt, &record);
        if (callback(&record, context) != 0) break;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE || rc == SQLITE_ROW ? 0 : -1;
}

int geoban_db_match(GeoBanDb *db, const ClientGeoIP *geoip, GeoBanRecord *record) {
    static const char sql[] =
        "SELECT type,value,reason,set_by,created_at,expires_at FROM geo_bans "
        "WHERE expires_at=0 OR expires_at>unixepoch() ORDER BY type,value COLLATE NOCASE";
    sqlite3_stmt *stmt = NULL;
    int rc;
    char asn[32];
    if (db == NULL || db->handle == NULL || geoip == NULL || record == NULL) return -1;
    if (purge_expired(db) != 0) return -1;
    (void)snprintf(asn, sizeof(asn), "%u", geoip->asn);
    if (sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        GeoBanRecord candidate;
        int matched = 0;
        record_from_stmt(stmt, &candidate);
        switch (candidate.type) {
            case GEOBAN_COUNTRY:
                matched = geoip->country_code[0] != '\0' &&
                          strcasecmp(candidate.value, geoip->country_code) == 0;
                break;
            case GEOBAN_REGION:
                matched = geoip->region_code[0] != '\0' &&
                          strcasecmp(candidate.value, geoip->region_code) == 0;
                break;
            case GEOBAN_ASN:
                matched = geoip->asn != 0U && strcmp(candidate.value, asn) == 0;
                break;
            case GEOBAN_ORG:
                matched = geoip->organization[0] != '\0' &&
                          glob_match_ci(candidate.value, geoip->organization);
                break;
            default:
                break;
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
