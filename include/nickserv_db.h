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
    char email[IRCD_EMAIL_MAX + 1U];
    int email_verified;
    char pending_email[IRCD_EMAIL_MAX + 1U];
    char email_verify_token_hash[IRCD_TOKEN_HASH_HEX_LEN + 1U];
    long long email_verify_expires_at;
    char reset_token_hash[IRCD_TOKEN_HASH_HEX_LEN + 1U];
    long long reset_expires_at;
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

/** Begin verification of a newly supplied email address. */
int nickserv_db_set_email_challenge(NickServDb *db, const char *name,
                                    const char *pending_email,
                                    const char *token_hash,
                                    long long expires_at);

/** Promote pending_email to the verified email when token and expiry match. */
int nickserv_db_verify_email(NickServDb *db, const char *name,
                             const char *token_hash, long long now);

/** Network-administrator direct email assignment; an empty address clears it. */
int nickserv_db_admin_set_email(NickServDb *db, const char *name,
                                const char *email, int verified);

/** Store/consume a one-time password reset token. */
int nickserv_db_set_reset_token(NickServDb *db, const char *name,
                                const char *token_hash, long long expires_at);
int nickserv_db_consume_reset_token(NickServDb *db, const char *name,
                                    const char *token_hash, long long now,
                                    const char *new_password_hash);

#endif /* IRCD_NICKSERV_DB_H */
