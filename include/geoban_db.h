#ifndef IRCD_GEOBAN_DB_H
#define IRCD_GEOBAN_DB_H

/**
 * @file geoban_db.h
 * @brief Persistent MaxMind metadata policy bans stored in bans.db.
 *
 * GeoBAN policy is deliberately separate from KLINE/ZLINE. It matches only
 * fields copied into Client.geoip from the configured MaxMind databases.
 */

#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "geoip.h"

typedef enum GeoBanType {
    GEOBAN_COUNTRY = 1,
    GEOBAN_REGION = 2,
    GEOBAN_ASN = 3,
    GEOBAN_ORG = 4
} GeoBanType;

typedef struct GeoBanDb {
    sqlite3 *handle;
} GeoBanDb;

typedef struct GeoBanRecord {
    GeoBanType type;
    char value[IRCD_GEOIP_ORG_MAX + 1U];
    char reason[IRC_QUIT_REASON_MAX + 1U];
    char set_by[IRCD_OPER_NAME_MAX + 1U];
    long long created_at;
    long long expires_at; /* 0 = permanent */
} GeoBanRecord;

typedef int (*GeoBanListCallback)(const GeoBanRecord *record, void *context);

int geoban_db_open(GeoBanDb *db, const char *path);
void geoban_db_close(GeoBanDb *db);
int geoban_db_add(GeoBanDb *db, GeoBanType type, const char *value,
                  const char *reason, const char *set_by,
                  unsigned int duration_seconds);
int geoban_db_delete(GeoBanDb *db, GeoBanType type, const char *value);
int geoban_db_list(GeoBanDb *db, GeoBanListCallback callback, void *context);
int geoban_db_match(GeoBanDb *db, const ClientGeoIP *geoip, GeoBanRecord *record);

const char *geoban_type_name(GeoBanType type);
int geoban_type_parse(const char *text, GeoBanType *type);
int geoban_normalize_value(GeoBanType type, const char *input,
                           char *output, size_t output_size);
int geoban_duration_parse(const char *text, unsigned int *seconds);

#endif /* IRCD_GEOBAN_DB_H */
