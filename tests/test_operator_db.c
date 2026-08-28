/**
 * @file test_operator_db.c
 * @brief Unit test for the operators.db schema and CRUD helpers.
 */

#include "operator_db.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int seen_count = 0;

static int count_record(const OperatorRecord *record, void *context) {
    (void)context;
    assert(record != NULL);
    ++seen_count;
    return 0;
}

static void raw_set_text(sqlite3 *db, const char *column,
                         const char *name, const char *value) {
    sqlite3_stmt *stmt = NULL;
    char sql[160];
    int written = snprintf(sql, sizeof(sql),
                           "UPDATE operators SET %s=?1 WHERE name=?2", column);
    assert(written > 0 && (size_t)written < sizeof(sql));
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, value, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_changes(db) == 1);
    sqlite3_finalize(stmt);
}

static void fill_overlong(char *buffer, size_t valid_max, char ch) {
    memset(buffer, ch, valid_max + 1U);
    buffer[valid_max + 1U] = '\0';
}

static void inject_oversized_vhost(sqlite3 *db, const char *name) {
    char vhost[IRC_HOST_MAX + 2U];
    fill_overlong(vhost, IRC_HOST_MAX, 'v');
    raw_set_text(db, "vhost", name, vhost);
}

int main(void) {
    char path[128];
    OperatorDb db;
    OperatorRecord add;
    OperatorRecord got;
    OperatorRecord invalid;
    char long_name[IRCD_OPER_NAME_MAX + 2U];
    char long_hash[IRCD_OPER_HASH_MAX + 2U];
    char long_permissions[IRCD_OPER_FLAGS_MAX + 2U];
    char long_vhost[IRCD_OPER_VHOST_MAX + 2U];

    fill_overlong(long_name, IRCD_OPER_NAME_MAX, 'N');
    fill_overlong(long_hash, IRCD_OPER_HASH_MAX, 'H');
    fill_overlong(long_permissions, IRCD_OPER_FLAGS_MAX, 'P');
    fill_overlong(long_vhost, IRCD_OPER_VHOST_MAX, 'V');

    (void)snprintf(path, sizeof(path), "/tmp/scratchircd-operators-%ld.db",
                   (long)getpid());
    unlink(path);

    assert(operator_db_open(&db, path) == 0);

    memset(&add, 0, sizeof(add));
    snprintf(add.name, sizeof(add.name), "%s", "TestOper");
    snprintf(add.password_hash, sizeof(add.password_hash), "%s", "$argon2id$dummy");
    snprintf(add.permissions, sizeof(add.permissions), "%s", "can_kill,can_kline");
    snprintf(add.vhost, sizeof(add.vhost), "%s", "oper.test");
    add.enabled = 1;

    assert(operator_db_add(&db, &add) == 0);
    assert(operator_db_get(&db, "testoper", &got) == 1);
    assert(strcmp(got.name, "TestOper") == 0);
    assert(strcmp(got.permissions, "can_kill,can_kline") == 0);
    assert(strcmp(got.vhost, "oper.test") == 0);
    assert(got.enabled == 1);
    assert(got.created_at > 0);
    assert(got.updated_at > 0);

    /* Public write/query APIs reject fields that cannot round-trip through
     * OperatorRecord instead of relying on SQLite to accept them. */
    assert(operator_db_get(&db, long_name, &got) == -1);
    assert(operator_db_delete(&db, long_name) == -1);
    assert(operator_db_set_name(&db, "TestOper", long_name) == -1);
    assert(operator_db_set_password(&db, "TestOper", long_hash) == -1);
    assert(operator_db_set_permissions(&db, "TestOper", long_permissions) == -1);
    assert(operator_db_set_vhost(&db, "TestOper", long_vhost) == -1);

    memset(&invalid, 0, sizeof(invalid));
    snprintf(invalid.name, sizeof(invalid.name), "%s", "Invalid");
    snprintf(invalid.password_hash, sizeof(invalid.password_hash), "%s", "$argon2id$invalid");
    invalid.enabled = 1;
    /* Arrays cannot contain a C string longer than their declared capacity,
     * so ADD validation is exercised for required-empty identity/hash fields. */
    invalid.name[0] = '\0';
    assert(operator_db_add(&db, &invalid) == -1);
    snprintf(invalid.name, sizeof(invalid.name), "%s", "Invalid");
    invalid.password_hash[0] = '\0';
    assert(operator_db_add(&db, &invalid) == -1);

    /* Direct legacy/external corruption must fail closed, including LIST. */
    raw_set_text(db.handle, "password_hash", "TestOper", long_hash);
    assert(operator_db_get(&db, "TestOper", &got) == -1);
    seen_count = 0;
    assert(operator_db_list(&db, count_record, NULL) == -1);
    assert(seen_count == 0);
    raw_set_text(db.handle, "password_hash", "TestOper", "$argon2id$dummy");
    assert(operator_db_get(&db, "TestOper", &got) == 1);

    raw_set_text(db.handle, "permissions", "TestOper", long_permissions);
    assert(operator_db_get(&db, "TestOper", &got) == -1);
    assert(operator_db_list(&db, count_record, NULL) == -1);
    raw_set_text(db.handle, "permissions", "TestOper", "can_kill,can_kline");
    assert(operator_db_get(&db, "TestOper", &got) == 1);

    assert(operator_db_set_permissions(&db, "TESTOPER", "can_kill") == 0);
    assert(operator_db_set_vhost(&db, "testoper", "new.oper.test") == 0);
    assert(operator_db_set_enabled(&db, "TestOper", 0) == 0);
    assert(operator_db_set_name(&db, "testoper", "RenamedOper") == 0);
    assert(operator_db_get(&db, "testoper", &got) == 0);
    assert(operator_db_get(&db, "renamedoper", &got) == 1);
    assert(strcmp(got.name, "RenamedOper") == 0);
    assert(strcmp(got.permissions, "can_kill") == 0);
    assert(strcmp(got.vhost, "new.oper.test") == 0);
    assert(got.enabled == 0);

    seen_count = 0;
    assert(operator_db_list(&db, count_record, NULL) == 0);
    assert(seen_count == 1);

    inject_oversized_vhost(db.handle, "RenamedOper");
    operator_db_close(&db);
    assert(operator_db_open(&db, path) != 0);
    assert(db.handle == NULL);

    unlink(path);
    return 0;
}
