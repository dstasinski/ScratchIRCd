#ifndef IRCD_BAN_DB_H
#define IRCD_BAN_DB_H

/**
 * @file ban_db.h
 * @brief SQLite-backed persistent KLINE/ZLINE and E-LINE storage.
 */

#include <sqlite3.h>
#include "config.h"

typedef enum BanType {
    BAN_TYPE_KLINE = 1,
    BAN_TYPE_ZLINE = 2
} BanType;

typedef enum BanExceptionType {
    BAN_EXCEPTION_KLINE = 1,
    BAN_EXCEPTION_ZLINE = 2,
    BAN_EXCEPTION_CONNECTION_LIMIT = 3,
    BAN_EXCEPTION_DNSBL = 4,
    BAN_EXCEPTION_GEOBAN = 5
} BanExceptionType;

typedef struct BanDb {
    sqlite3 *handle;
} BanDb;

typedef struct BanRecord {
    BanType type;
    char mask[IRC_CHANNEL_MASK_MAX + 1U];
    char reason[IRC_QUIT_REASON_MAX + 1U];
    char set_by[IRCD_OPER_NAME_MAX + 1U];
    long long created_at;
    /** Unix timestamp when the ban expires; zero means permanent. */
    long long expires_at;
} BanRecord;

typedef struct BanExceptionRecord {
    BanExceptionType type;
    char mask[IRC_CHANNEL_MASK_MAX + 1U];
    char reason[IRC_QUIT_REASON_MAX + 1U];
    char set_by[IRCD_OPER_NAME_MAX + 1U];
    long long created_at;
    /** Unix timestamp when the exception expires; zero means permanent. */
    long long expires_at;
} BanExceptionRecord;

typedef int (*BanDbListCallback)(const BanRecord *record, void *context);
typedef int (*BanExceptionDbListCallback)(const BanExceptionRecord *record,
                                          void *context);

int ban_db_open(BanDb *db, const char *path);
void ban_db_close(BanDb *db);
/** Reset process-local expired-row maintenance throttle for shutdown/RESTART. */
void ban_db_reset_runtime_state(void);
int ban_db_add(BanDb *db, BanType type, const char *mask,
               const char *reason, const char *set_by);
int ban_db_add_timed(BanDb *db, BanType type, const char *mask,
                     const char *reason, const char *set_by,
                     unsigned int duration_seconds);
int ban_db_delete(BanDb *db, BanType type, const char *mask);
int ban_db_list(BanDb *db, BanType type, BanDbListCallback callback, void *context);
int ban_db_match(BanDb *db, BanType type, const char *identity1,
                 const char *identity2, BanRecord *record);
/** Return non-zero when a loaded ban record matches either supplied identity. */
int ban_record_matches(const BanRecord *record, const char *identity1,
                       const char *identity2);

int ban_exception_db_add(BanDb *db, BanExceptionType type, const char *mask,
                         const char *reason, const char *set_by);
int ban_exception_db_add_timed(BanDb *db, BanExceptionType type, const char *mask,
                               const char *reason, const char *set_by,
                               unsigned int duration_seconds);
int ban_exception_db_delete(BanDb *db, BanExceptionType type, const char *mask);
int ban_exception_db_delete_mask(BanDb *db, const char *mask);
int ban_exception_db_list(BanDb *db, BanExceptionType type,
                          BanExceptionDbListCallback callback, void *context);
int ban_exception_db_list_all(BanDb *db, BanExceptionDbListCallback callback,
                              void *context);
int ban_exception_db_match(BanDb *db, BanExceptionType type,
                           const char *identity1, const char *identity2,
                           BanExceptionRecord *record);
/** Return non-zero when a loaded exception record matches either identity. */
int ban_exception_record_matches(const BanExceptionRecord *record,
                                 const char *identity1, const char *identity2);

int ban_db_purge_expired(BanDb *db);

#endif /* IRCD_BAN_DB_H */
