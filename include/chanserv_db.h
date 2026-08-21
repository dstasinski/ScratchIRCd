#ifndef IRCD_CHANSERV_DB_H
#define IRCD_CHANSERV_DB_H

#include <sqlite3.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"

/** Persistent ChanServ channel registration and settings. */
typedef struct ChanServChannel {
    char name[IRC_CHANNEL_NAME_MAX + 1U];
    char founder[IRC_NICK_MAX + 1U];
    char description[IRCD_CHANSERV_DESCRIPTION_MAX + 1U];
    int enabled;
    uint64_t mode_lock;
    char topic[IRC_CHANNEL_TOPIC_MAX + 1U];
    char topic_setter[IRC_CHANNEL_TOPIC_SETTER_MAX + 1U];
    long long topic_time;
    long long created_at;
    long long updated_at;
} ChanServChannel;

typedef enum ChanServAccessLevel {
    CHANSERV_ACCESS_NONE = 0,
    CHANSERV_ACCESS_VOICE = 1,
    CHANSERV_ACCESS_HALFOP = 2,
    CHANSERV_ACCESS_OP = 3,
    /* Keep OWNER at 4 for compatibility with existing 0.19 databases. */
    CHANSERV_ACCESS_OWNER = 4,
    CHANSERV_ACCESS_PROTECTED = 5
} ChanServAccessLevel;

typedef struct ChanServAccess {
    char channel[IRC_CHANNEL_NAME_MAX + 1U];
    char account[IRC_NICK_MAX + 1U];
    ChanServAccessLevel level;
} ChanServAccess;

typedef struct ChanServDb { sqlite3 *db; } ChanServDb;

int chanserv_db_open(ChanServDb *db, const char *path);
void chanserv_db_close(ChanServDb *db);
int chanserv_db_get(ChanServDb *db, const char *name, ChanServChannel *record);
int chanserv_db_create(ChanServDb *db, const char *name, const char *founder, const char *description);
int chanserv_db_delete(ChanServDb *db, const char *name);
int chanserv_db_set_description(ChanServDb *db, const char *name, const char *description);
int chanserv_db_set_founder(ChanServDb *db, const char *name, const char *founder);
int chanserv_db_set_enabled(ChanServDb *db, const char *name, int enabled);
int chanserv_db_set_mode_lock(ChanServDb *db, const char *name, uint64_t mode_lock);
int chanserv_db_set_topic(ChanServDb *db, const char *name, const char *topic,
                          const char *setter, long long topic_time);
int chanserv_db_list_enabled(ChanServDb *db, char *buffer, size_t size);

int chanserv_db_access_set(ChanServDb *db, const char *channel, const char *account,
                           ChanServAccessLevel level);
int chanserv_db_access_delete(ChanServDb *db, const char *channel, const char *account);
int chanserv_db_access_get(ChanServDb *db, const char *channel, const char *account,
                           ChanServAccess *record);
int chanserv_db_access_list(ChanServDb *db, const char *channel, char *buffer, size_t size);

#endif
