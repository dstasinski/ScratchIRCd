#ifndef IRCD_CHANSERV_DB_H
#define IRCD_CHANSERV_DB_H

#include <sqlite3.h>
#include <stddef.h>
#include "config.h"

/** Persistent ChanServ channel registration. */
typedef struct ChanServChannel {
    char name[IRC_CHANNEL_NAME_MAX + 1U];
    char founder[IRC_NICK_MAX + 1U];
    char description[IRCD_CHANSERV_DESCRIPTION_MAX + 1U];
    int enabled;
    long long created_at;
    long long updated_at;
} ChanServChannel;

typedef struct ChanServDb { sqlite3 *db; } ChanServDb;

int chanserv_db_open(ChanServDb *db, const char *path);
void chanserv_db_close(ChanServDb *db);
int chanserv_db_get(ChanServDb *db, const char *name, ChanServChannel *record);
int chanserv_db_create(ChanServDb *db, const char *name, const char *founder, const char *description);
int chanserv_db_delete(ChanServDb *db, const char *name);
int chanserv_db_set_description(ChanServDb *db, const char *name, const char *description);
int chanserv_db_set_founder(ChanServDb *db, const char *name, const char *founder);
int chanserv_db_set_enabled(ChanServDb *db, const char *name, int enabled);
int chanserv_db_list_enabled(ChanServDb *db, char *buffer, size_t size);

#endif
