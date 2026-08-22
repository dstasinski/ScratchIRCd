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

int main(void) {
    char path[128];
    GeoBanDb db;
    GeoBanRecord match;
    ClientGeoIP geo;
    GeoBanType type;
    unsigned int seconds;
    char normalized[256];

    (void)snprintf(path, sizeof(path), "/tmp/scratchircd-geobans-%ld.db", (long)getpid());
    unlink(path);

    assert(geoban_type_parse("country", &type) == 0 && type == GEOBAN_COUNTRY);
    assert(geoban_normalize_value(GEOBAN_COUNTRY, "ru", normalized, sizeof(normalized)) == 0);
    assert(strcmp(normalized, "RU") == 0);
    assert(geoban_normalize_value(GEOBAN_ASN, "AS22773", normalized, sizeof(normalized)) == 0);
    assert(strcmp(normalized, "22773") == 0);
    assert(geoban_duration_parse("30s", &seconds) == 0 && seconds == 30U);
    assert(geoban_duration_parse("5m", &seconds) == 0 && seconds == 300U);
    assert(geoban_duration_parse("2h", &seconds) == 0 && seconds == 7200U);
    assert(geoban_duration_parse("7d", &seconds) == 0 && seconds == 604800U);
    assert(geoban_duration_parse("2w", &seconds) == 0 && seconds == 1209600U);
    assert(geoban_duration_parse("forever", &seconds) == 0 && seconds == 0U);

    assert(geoban_db_open(&db, path) == 0);
    assert(geoban_db_add(&db, GEOBAN_COUNTRY, "RU", "country", "root", 0U) == 0);
    assert(geoban_db_add(&db, GEOBAN_REGION, "AZ", "region", "root", 0U) == 0);
    assert(geoban_db_add(&db, GEOBAN_ASN, "22773", "asn", "root", 0U) == 0);
    assert(geoban_db_add(&db, GEOBAN_ORG, "*Example Network*", "org", "root", 0U) == 0);

    memset(&geo, 0, sizeof(geo));
    snprintf(geo.country_code, sizeof(geo.country_code), "ru");
    assert(geoban_db_match(&db, &geo, &match) == 1);
    assert(match.type == GEOBAN_COUNTRY);

    memset(&geo, 0, sizeof(geo));
    snprintf(geo.region_code, sizeof(geo.region_code), "az");
    assert(geoban_db_match(&db, &geo, &match) == 1);
    assert(match.type == GEOBAN_REGION);

    memset(&geo, 0, sizeof(geo));
    geo.asn = 22773U;
    assert(geoban_db_match(&db, &geo, &match) == 1);
    assert(match.type == GEOBAN_ASN);

    memset(&geo, 0, sizeof(geo));
    snprintf(geo.organization, sizeof(geo.organization), "ACME Example Network LLC West");
    assert(geoban_db_match(&db, &geo, &match) == 1);
    assert(match.type == GEOBAN_ORG);

    listed = 0;
    assert(geoban_db_list(&db, count_record, NULL) == 0);
    assert(listed == 4);

    assert(geoban_db_delete(&db, GEOBAN_COUNTRY, "RU") == 0);
    geoban_db_close(&db);

    /* Persistence across reopen. */
    assert(geoban_db_open(&db, path) == 0);
    listed = 0;
    assert(geoban_db_list(&db, count_record, NULL) == 0);
    assert(listed == 3);
    geoban_db_close(&db);
    unlink(path);
    return 0;
}
