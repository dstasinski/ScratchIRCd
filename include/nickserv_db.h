#ifndef IRCD_NICKSERV_DB_H
#define IRCD_NICKSERV_DB_H

/**
 * @file nickserv_db.h
 * @brief SQLite-backed registered-nickname/account storage for NickServ.
 */

#include <sqlite3.h>
#include "config.h"

typedef struct NickServDb {
    sqlite3 *handle;
} NickServDb;

typedef struct NickServAccount {
    char name[IRC_NICK_MAX + 1U];
    char password_hash[IRCD_OPER_HASH_MAX + 1U];
    char vhost[IRC_HOST_MAX + 1U];
    int enabled;
    long long created_at;
    long long updated_at;
} NickServAccount;

int nickserv_db_open(NickServDb *db, const char *path);
void nickserv_db_close(NickServDb *db);
int nickserv_db_get(NickServDb *db, const char *name, NickServAccount *account);
int nickserv_db_add(NickServDb *db, const NickServAccount *account);
int nickserv_db_delete(NickServDb *db, const char *name);
int nickserv_db_set_password(NickServDb *db, const char *name, const char *password_hash);
int nickserv_db_set_vhost(NickServDb *db, const char *name, const char *vhost);
int nickserv_db_set_enabled(NickServDb *db, const char *name, int enabled);

#endif /* IRCD_NICKSERV_DB_H */
