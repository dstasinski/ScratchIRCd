#include "nickserv_db.h"
#include "operator_db.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void make_temp_path(char *path, size_t path_size, const char *prefix) {
    int fd;
    int written;

    assert(path != NULL);
    assert(prefix != NULL);
    written = snprintf(path, path_size, "/tmp/%s-XXXXXX", prefix);
    assert(written > 0 && (size_t)written < path_size);
    fd = mkstemp(path);
    assert(fd >= 0);
    close(fd);
    assert(unlink(path) == 0);
}

static void exec_sql(sqlite3 *db, const char *sql) {
    char *error = NULL;
    assert(db != NULL);
    assert(sql != NULL);
    assert(sqlite3_exec(db, sql, NULL, NULL, &error) == SQLITE_OK);
    sqlite3_free(error);
}

static void create_incomplete_nickserv_db(const char *path) {
    sqlite3 *db = NULL;

    assert(sqlite3_open(path, &db) == SQLITE_OK);
    exec_sql(db,
        "CREATE TABLE nickserv_accounts ("
        "name TEXT COLLATE NOCASE PRIMARY KEY,"
        "password_hash TEXT NOT NULL,"
        "vhost TEXT NOT NULL DEFAULT '',"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "updated_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "email TEXT NOT NULL DEFAULT '',"
        "email_verified INTEGER NOT NULL DEFAULT 0,"
        "pending_email TEXT NOT NULL DEFAULT '',"
        "email_verify_token_hash TEXT NOT NULL DEFAULT '',"
        "email_verify_expires_at INTEGER NOT NULL DEFAULT 0,"
        "reset_token_hash TEXT NOT NULL DEFAULT '',"
        "reset_expires_at INTEGER NOT NULL DEFAULT 0"
        ");");
    assert(sqlite3_close(db) == SQLITE_OK);
}

static void create_incomplete_operator_db(const char *path) {
    sqlite3 *db = NULL;

    assert(sqlite3_open(path, &db) == SQLITE_OK);
    exec_sql(db,
        "CREATE TABLE operators ("
        "name TEXT COLLATE NOCASE,"
        "password_hash TEXT NOT NULL,"
        "permissions TEXT NOT NULL DEFAULT '',"
        "vhost TEXT NOT NULL,"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "updated_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "PRIMARY KEY(name)"
        ");");
    assert(sqlite3_close(db) == SQLITE_OK);
}

int main(void) {
    char nickserv_path[128];
    char operator_path[128];
    NickServDb nickserv = {0};
    OperatorDb operators = {0};

    make_temp_path(nickserv_path, sizeof(nickserv_path),
                   "scratchircd-current-nickserv");
    create_incomplete_nickserv_db(nickserv_path);
    assert(nickserv_db_open(&nickserv, nickserv_path) != 0);
    assert(nickserv.handle == NULL);
    assert(unlink(nickserv_path) == 0);

    make_temp_path(operator_path, sizeof(operator_path),
                   "scratchircd-current-operators");
    create_incomplete_operator_db(operator_path);
    assert(operator_db_open(&operators, operator_path) != 0);
    assert(operators.handle == NULL);
    assert(unlink(operator_path) == 0);

    return 0;
}
