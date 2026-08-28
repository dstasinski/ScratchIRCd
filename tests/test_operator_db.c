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

static void inject_oversized_vhost(sqlite3 *db, const char *name) {
    sqlite3_stmt *stmt = NULL;
    char vhost[IRC_HOST_MAX + 2U];
    memset(vhost, 'v', IRC_HOST_MAX + 1U);
    vhost[IRC_HOST_MAX + 1U] = '\0';
    assert(sqlite3_prepare_v2(db,
        "UPDATE operators SET vhost=?1 WHERE name=?2",
        -1, &stmt, NULL) == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, vhost, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_changes(db) == 1);
    sqlite3_finalize(stmt);
}

int main(void) {
    char path[128];
    OperatorDb db;
    OperatorRecord add;
    OperatorRecord got;

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
