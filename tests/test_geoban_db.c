/** @file test_geoban_db.c @brief Unit tests for persistent GeoIP policy bans. */

#include "geoban_db.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int listed = 0;
static int count_record(const GeoBanRecord *record, void *context) {
    (void)context;
    assert(record != NULL);
    ++listed;
    return 0;
}

static void fill_overlong(char *buffer, size_t maximum, char ch) {
    memset(buffer, ch, maximum + 1U);
    buffer[maximum + 1U] = '\0';
}

static void raw_set_text(sqlite3 *db, GeoBanType type, const char *lookup_value,
                         const char *column, const char *value) {
    sqlite3_stmt *stmt = NULL;
    char sql[160];
    int written = snprintf(sql, sizeof(sql),
                           "UPDATE geo_bans SET %s=?1 WHERE type=?2 AND value=?3", column);
    assert(written > 0 && (size_t)written < sizeof(sql));
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, (int)type);
    sqlite3_bind_text(stmt, 3, lookup_value, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_changes(db) == 1);
    sqlite3_finalize(stmt);
}

int main(void) {
    char path[128];
    GeoBanDb db;
    GeoBanRecord match;
    ClientGeoIP geo;
    GeoBanType type;
    unsigned int seconds;
    char normalized[256];
    char long_value[IRCD_GEOIP_ORG_MAX + 2U];
    char long_reason[IRC_QUIT_REASON_MAX + 2U];
    char long_set_by[IRCD_OPER_NAME_MAX + 2U];

    fill_overlong(long_value, IRCD_GEOIP_ORG_MAX, 'V');
    fill_overlong(long_reason, IRC_QUIT_REASON_MAX, 'R');
    fill_overlong(long_set_by, IRCD_OPER_NAME_MAX, 'S');

    (void)snprintf(path, sizeof(path), "/tmp/scratchircd-geobans-%ld.db", (long)getpid());
    unlink(path);

    assert(geoban_type_parse("country", &type) == 0 && type == GEOBAN_COUNTRY);
    assert(geoban_normalize_value(GEOBAN_COUNTRY, "ru", normalized, sizeof(normalized)) == 0);
    assert(strcmp(normalized, "RU") == 0);
    assert(geoban_normalize_value(GEOBAN_ASN, "AS22773", normalized, sizeof(normalized)) == 0);
    assert(strcmp(normalized, "22773") == 0);
    assert(geoban_normalize_value(GEOBAN_ORG, "bad\norg", normalized, sizeof(normalized)) == -1);
    assert(geoban_duration_parse("30s", &seconds) == 0 && seconds == 30U);
    assert(geoban_duration_parse("5m", &seconds) == 0 && seconds == 300U);
    assert(geoban_duration_parse("2h", &seconds) == 0 && seconds == 7200U);
    assert(geoban_duration_parse("7d", &seconds) == 0 && seconds == 604800U);
    assert(geoban_duration_parse("2w", &seconds) == 0 && seconds == 1209600U);
    assert(geoban_duration_parse("forever", &seconds) == 0 && seconds == 0U);

    assert(geoban_db_open(&db, path) == 0);
    assert(geoban_db_add(&db, (GeoBanType)99, "RU", "bad type", "root", 0U) == -1);
    assert(geoban_db_add(&db, GEOBAN_ORG, long_value, "bad value", "root", 0U) == -1);
    assert(geoban_db_add(&db, GEOBAN_ORG, "Example", long_reason, "root", 0U) == -1);
    assert(geoban_db_add(&db, GEOBAN_ORG, "Example", "reason", long_set_by, 0U) == -1);
    assert(geoban_db_add(&db, GEOBAN_ORG, "bad\nvalue", "reason", "root", 0U) == -1);
    assert(geoban_db_add(&db, GEOBAN_ORG, "Example", "bad\rreason", "root", 0U) == -1);
    assert(geoban_db_delete(&db, (GeoBanType)99, "RU") == -1);
    assert(geoban_db_delete(&db, GEOBAN_ORG, "bad\nvalue") == -1);

    assert(geoban_db_add(&db, GEOBAN_COUNTRY, "RU", "country", "root", 0U) == 0);
    assert(geoban_db_add(&db, GEOBAN_REGION, "AZ", "region", "root", 0U) == 0);
    assert(geoban_db_add(&db, GEOBAN_ASN, "22773", "asn", "root", 0U) == 0);
    assert(geoban_db_add(&db, GEOBAN_ORG, "*Example Network*", "org", "root", 0U) == 0);

    memset(&geo, 0, sizeof(geo));
    snprintf(geo.country_code, sizeof(geo.country_code), "ru");
    assert(geoban_db_match(&db, &geo, &match) == 1);
    assert(match.type == GEOBAN_COUNTRY);
    assert(geoban_record_matches(&match, &geo));

    memset(&geo, 0, sizeof(geo));
    snprintf(geo.region_code, sizeof(geo.region_code), "az");
    assert(geoban_db_match(&db, &geo, &match) == 1);
    assert(match.type == GEOBAN_REGION);
    assert(geoban_record_matches(&match, &geo));

    memset(&geo, 0, sizeof(geo));
    geo.asn = 22773U;
    assert(geoban_db_match(&db, &geo, &match) == 1);
    assert(match.type == GEOBAN_ASN);
    assert(geoban_record_matches(&match, &geo));

    memset(&geo, 0, sizeof(geo));
    snprintf(geo.organization, sizeof(geo.organization), "ACME Example Network LLC West");
    assert(geoban_db_match(&db, &geo, &match) == 1);
    assert(match.type == GEOBAN_ORG);
    assert(geoban_record_matches(&match, &geo));

    memset(&geo, 0, sizeof(geo));
    snprintf(geo.organization, sizeof(geo.organization), "Unrelated Transit LLC");
    assert(!geoban_record_matches(&match, &geo));

    listed = 0;
    assert(geoban_db_list(&db, count_record, NULL) == 0);
    assert(listed == 4);

    /* Legacy/external corruption must be rejected exactly, never clipped into
     * a different policy identity or human-readable reason. */
    raw_set_text(db.handle, GEOBAN_REGION, "AZ", "reason", long_reason);
    listed = 0;
    assert(geoban_db_list(&db, count_record, NULL) == -1);
    memset(&geo, 0, sizeof(geo));
    snprintf(geo.region_code, sizeof(geo.region_code), "az");
    assert(geoban_db_match(&db, &geo, &match) == -1);
    raw_set_text(db.handle, GEOBAN_REGION, "AZ", "reason", "region");

    raw_set_text(db.handle, GEOBAN_ASN, "22773", "set_by", "ro\not");
    assert(geoban_db_list(&db, count_record, NULL) == -1);
    raw_set_text(db.handle, GEOBAN_ASN, "22773", "set_by", "root");

    raw_set_text(db.handle, GEOBAN_ORG, "*Example Network*", "value", long_value);
    assert(geoban_db_list(&db, count_record, NULL) == -1);
    raw_set_text(db.handle, GEOBAN_ORG, long_value, "value", "*Example Network*");
    listed = 0;
    assert(geoban_db_list(&db, count_record, NULL) == 0);
    assert(listed == 4);

    assert(geoban_db_delete(&db, GEOBAN_COUNTRY, "RU") == 0);

    /* Force a temporary row into the past and verify active-list purge. */
    assert(geoban_db_add(&db, GEOBAN_COUNTRY, "CA", "temporary", "root", 60U) == 0);
    assert(sqlite3_exec(db.handle,
        "UPDATE geo_bans SET expires_at=unixepoch()-1 WHERE type=1 AND value='CA'",
        NULL, NULL, NULL) == SQLITE_OK);
    listed = 0;
    assert(geoban_db_list(&db, count_record, NULL) == 0);
    assert(listed == 3);
    geoban_db_close(&db);

    /* Persistence across reopen. Expired rows remain purged. */
    assert(geoban_db_open(&db, path) == 0);
    listed = 0;
    assert(geoban_db_list(&db, count_record, NULL) == 0);
    assert(listed == 3);
    geoban_db_close(&db);
    unlink(path);
    return 0;
}
