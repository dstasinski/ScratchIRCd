#include "nickserv_db.h"
#include "sqlite_policy.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int pragma_int(sqlite3 *db, const char *sql) {
    sqlite3_stmt *stmt = NULL;
    int value = -1;
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

static void raw_set_text(sqlite3 *db, const char *column,
                         const char *name, const char *value) {
    sqlite3_stmt *stmt = NULL;
    char sql[192];
    int written = snprintf(sql, sizeof(sql),
                           "UPDATE nickserv_accounts SET %s=?1 WHERE name=?2", column);
    assert(written > 0 && (size_t)written < sizeof(sql));
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_changes(db) == 1);
    sqlite3_finalize(stmt);
}

static void inject_oversized_vhost(sqlite3 *db, const char *name) {
    char vhost[IRC_HOST_MAX + 2U];
    memset(vhost, 'v', IRC_HOST_MAX + 1U);
    vhost[IRC_HOST_MAX + 1U] = '\0';
    raw_set_text(db, "vhost", name, vhost);
}

static void fill_overlong(char *buffer, size_t valid_max, char ch) {
    memset(buffer, ch, valid_max + 1U);
    buffer[valid_max + 1U] = '\0';
}

int main(void) {
    char path[] = "/tmp/scratchircd-nickserv-XXXXXX";
    int fd = mkstemp(path);
    NickServDb db = {0};
    NickServAccount account;
    NickServAccount loaded;
    NickServAccount second;
    NickServAccount third;
    char long_name[IRC_NICK_MAX + 2U];
    char long_hash[IRCD_OPER_HASH_MAX + 2U];
    char long_vhost[IRC_HOST_MAX + 2U];
    char long_email[IRCD_EMAIL_MAX + 2U];
    char long_token[IRCD_TOKEN_HASH_HEX_LEN + 2U];

    fill_overlong(long_name, IRC_NICK_MAX, 'N');
    fill_overlong(long_hash, IRCD_OPER_HASH_MAX, 'H');
    fill_overlong(long_vhost, IRC_HOST_MAX, 'V');
    fill_overlong(long_email, IRCD_EMAIL_MAX, 'E');
    fill_overlong(long_token, IRCD_TOKEN_HASH_HEX_LEN, 'T');

    assert(fd >= 0);
    close(fd);
    assert(unlink(path) == 0);

    assert(nickserv_db_open(&db, path) == 0);
    assert(pragma_int(db.handle, "PRAGMA busy_timeout") == IRCD_SQLITE_BUSY_TIMEOUT_MS);
    assert(pragma_int(db.handle, "PRAGMA synchronous") == 1);
    assert(pragma_int(db.handle, "PRAGMA user_version") == 1);
    memset(&account, 0, sizeof(account));
    snprintf(account.name, sizeof(account.name), "%s", "Daniel");
    snprintf(account.password_hash, sizeof(account.password_hash), "%s", "$argon2id$test");
    account.enabled = 1;
    assert(nickserv_db_add(&db, &account) == 0);

    memset(&second, 0, sizeof(second));
    snprintf(second.name, sizeof(second.name), "%s", "Alice");
    snprintf(second.password_hash, sizeof(second.password_hash), "%s", "$argon2id$second");
    second.enabled = 1;
    assert(nickserv_db_add(&db, &second) == 0);

    memset(&third, 0, sizeof(third));
    snprintf(third.name, sizeof(third.name), "%s", "Overflow");
    snprintf(third.password_hash, sizeof(third.password_hash), "%s", "$argon2id$third");
    third.enabled = 1;
    assert(nickserv_db_add(&db, &third) != 0);
    assert(nickserv_db_get(&db, "Overflow", &loaded) == 0);

    assert(nickserv_db_get(&db, "daniel", &loaded) == 1);
    assert(strcmp(loaded.name, "Daniel") == 0);
    assert(loaded.enabled == 1);
    assert(loaded.email[0] == '\0');

    /* Public DB APIs reject values that cannot round-trip through the fixed
     * account representation rather than relying on SQLite to accept them. */
    assert(nickserv_db_get(&db, long_name, &loaded) == -1);
    assert(nickserv_db_delete(&db, long_name) == -1);
    assert(nickserv_db_set_password(&db, "Daniel", long_hash) == -1);
    assert(nickserv_db_set_vhost(&db, "Daniel", long_vhost) == -1);
    assert(nickserv_db_admin_set_email(&db, "Daniel", long_email, 1) == -1);
    assert(nickserv_db_set_email_challenge(&db, "Daniel", long_email,
                                           "verifyhash", 2000) == -1);
    assert(nickserv_db_set_email_challenge(&db, "Daniel", "daniel@example.test",
                                           long_token, 2000) == -1);
    assert(nickserv_db_verify_email(&db, "Daniel", long_token, 1000) == -1);
    assert(nickserv_db_set_reset_token(&db, "Daniel", long_token, 3000) == -1);
    assert(nickserv_db_consume_reset_token(&db, "Daniel", long_token, 2000,
                                           "$argon2id$new") == -1);
    assert(nickserv_db_consume_reset_token(&db, "Daniel", "resethash", 2000,
                                           long_hash) == -1);

    /* Legacy/external corruption must fail closed instead of being clipped. */
    raw_set_text(db.handle, "password_hash", "Daniel", long_hash);
    assert(nickserv_db_get(&db, "Daniel", &loaded) == -1);
    raw_set_text(db.handle, "password_hash", "Daniel", "$argon2id$test");
    assert(nickserv_db_get(&db, "Daniel", &loaded) == 1);

    raw_set_text(db.handle, "email", "Daniel", long_email);
    assert(nickserv_db_get(&db, "Daniel", &loaded) == -1);
    raw_set_text(db.handle, "email", "Daniel", "");
    assert(nickserv_db_get(&db, "Daniel", &loaded) == 1);

    raw_set_text(db.handle, "pending_email", "Daniel", long_email);
    assert(nickserv_db_get(&db, "Daniel", &loaded) == -1);
    raw_set_text(db.handle, "pending_email", "Daniel", "");
    assert(nickserv_db_get(&db, "Daniel", &loaded) == 1);

    raw_set_text(db.handle, "email_verify_token_hash", "Daniel", long_token);
    assert(nickserv_db_get(&db, "Daniel", &loaded) == -1);
    raw_set_text(db.handle, "email_verify_token_hash", "Daniel", "");
    assert(nickserv_db_get(&db, "Daniel", &loaded) == 1);

    raw_set_text(db.handle, "reset_token_hash", "Daniel", long_token);
    assert(nickserv_db_get(&db, "Daniel", &loaded) == -1);
    raw_set_text(db.handle, "reset_token_hash", "Daniel", "");
    assert(nickserv_db_get(&db, "Daniel", &loaded) == 1);

    assert(nickserv_db_set_email_challenge(&db, "Daniel", "daniel@example.test",
                                           "verifyhash", 2000) == 0);
    assert(nickserv_db_verify_email(&db, "daniel", "wrong", 1000) == 0);
    assert(nickserv_db_verify_email(&db, "daniel", "verifyhash", 1000) == 1);
    assert(nickserv_db_get(&db, "Daniel", &loaded) == 1);
    assert(strcmp(loaded.email, "daniel@example.test") == 0);
    assert(loaded.email_verified == 1);
    assert(loaded.pending_email[0] == '\0');

    assert(nickserv_db_set_reset_token(&db, "Daniel", "resethash", 3000) == 0);
    assert(nickserv_db_consume_reset_token(&db, "Daniel", "bad", 2000,
                                           "$argon2id$new") == 0);
    assert(nickserv_db_consume_reset_token(&db, "Daniel", "resethash", 2000,
                                           "$argon2id$new") == 1);
    assert(nickserv_db_get(&db, "Daniel", &loaded) == 1);
    assert(strcmp(loaded.password_hash, "$argon2id$new") == 0);
    assert(loaded.reset_token_hash[0] == '\0');

    assert(nickserv_db_admin_set_email(&db, "Daniel", "admin@example.test", 1) == 0);
    assert(nickserv_db_get(&db, "Daniel", &loaded) == 1);
    assert(strcmp(loaded.email, "admin@example.test") == 0);
    assert(loaded.email_verified == 1);

    assert(nickserv_db_set_vhost(&db, "DANIEL", "user.example.test") == 0);
    assert(nickserv_db_set_enabled(&db, "daniel", 0) == 0);
    assert(nickserv_db_get(&db, "Daniel", &loaded) == 1);
    assert(strcmp(loaded.vhost, "user.example.test") == 0);
    assert(loaded.enabled == 0);

    assert(nickserv_db_set_password(&db, "Daniel", "$argon2id$changed") == 0);
    assert(nickserv_db_get(&db, "daniel", &loaded) == 1);
    assert(strcmp(loaded.password_hash, "$argon2id$changed") == 0);

    assert(nickserv_db_delete(&db, "DANIEL") == 0);
    assert(nickserv_db_get(&db, "daniel", &loaded) == 0);
    assert(nickserv_db_add(&db, &third) == 0);
    assert(nickserv_db_delete(&db, "Alice") == 0);
    nickserv_db_close(&db);

    assert(nickserv_db_open(&db, path) == 0);
    assert(pragma_int(db.handle, "PRAGMA user_version") == 1);
    inject_oversized_vhost(db.handle, "Overflow");
    nickserv_db_close(&db);
    assert(nickserv_db_open(&db, path) != 0);
    assert(db.handle == NULL);
    assert(unlink(path) == 0);
    return 0;
}
