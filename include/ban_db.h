#ifndef IRCD_BAN_DB_H
#define IRCD_BAN_DB_H

/**
 * @file ban_db.h
 * @brief SQLite-backed persistent KLINE/ZLINE storage.
 */

#include <sqlite3.h>
#include "config.h"

typedef enum BanType {
    BAN_TYPE_KLINE = 1,
    BAN_TYPE_ZLINE = 2
} BanType;

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

typedef int (*BanDbListCallback)(const BanRecord *record, void *context);

int ban_db_open(BanDb *db, const char *path);
void ban_db_close(BanDb *db);
int ban_db_add(BanDb *db, BanType type, const char *mask,
               const char *reason, const char *set_by);
int ban_db_add_timed(BanDb *db, BanType type, const char *mask,
                     const char *reason, const char *set_by,
                     unsigned int duration_seconds);
int ban_db_delete(BanDb *db, BanType type, const char *mask);
int ban_db_list(BanDb *db, BanType type, BanDbListCallback callback, void *context);
int ban_db_match(BanDb *db, BanType type, const char *identity1,
                 const char *identity2, BanRecord *record);
int ban_db_purge_expired(BanDb *db);

#endif /* IRCD_BAN_DB_H */
