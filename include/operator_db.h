#ifndef IRCD_OPERATOR_DB_H
#define IRCD_OPERATOR_DB_H

/**
 * @file operator_db.h
 * @brief SQLite-backed IRC operator account storage.
 *
 * Ordinary IRC operators are persisted in operators.db.  The bootstrap
 * network administrator remains in ircd.conf so a fresh installation always
 * has a recovery path even when the operator database is empty or damaged.
 */

#include <stddef.h>
#include <sqlite3.h>

#include "config.h"

typedef struct OperatorDb {
    sqlite3 *handle;
} OperatorDb;

typedef struct OperatorRecord {
    char name[IRCD_OPER_NAME_MAX + 1U];
    char password_hash[IRCD_OPER_HASH_MAX + 1U];
    char permissions[IRCD_OPER_FLAGS_MAX + 1U];
    char vhost[IRCD_OPER_VHOST_MAX + 1U];
    int enabled;
    long long created_at;
    long long updated_at;
} OperatorRecord;

typedef int (*OperatorDbListCallback)(const OperatorRecord *record, void *context);

int operator_db_open(OperatorDb *db, const char *path);
void operator_db_close(OperatorDb *db);
int operator_db_get(OperatorDb *db, const char *name, OperatorRecord *record);
int operator_db_add(OperatorDb *db, const OperatorRecord *record);
int operator_db_delete(OperatorDb *db, const char *name);
int operator_db_set_password(OperatorDb *db, const char *name, const char *password_hash);
int operator_db_set_permissions(OperatorDb *db, const char *name, const char *permissions);
int operator_db_set_vhost(OperatorDb *db, const char *name, const char *vhost);
int operator_db_set_enabled(OperatorDb *db, const char *name, int enabled);
int operator_db_list(OperatorDb *db, OperatorDbListCallback callback, void *context);

#endif /* IRCD_OPERATOR_DB_H */
