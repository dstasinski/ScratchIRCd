#include "nickserv_db.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/scratchircd-nickserv-XXXXXX";
    int fd = mkstemp(path);
    NickServDb db = {0};
    NickServAccount account;
    NickServAccount loaded;

    assert(fd >= 0);
    close(fd);
    assert(unlink(path) == 0);

    assert(nickserv_db_open(&db, path) == 0);
    memset(&account, 0, sizeof(account));
    snprintf(account.name, sizeof(account.name), "%s", "Daniel");
    snprintf(account.password_hash, sizeof(account.password_hash), "%s", "$argon2id$test");
    account.enabled = 1;
    assert(nickserv_db_add(&db, &account) == 0);

    assert(nickserv_db_get(&db, "daniel", &loaded) == 1);
    assert(strcmp(loaded.name, "Daniel") == 0);
    assert(loaded.enabled == 1);

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
    nickserv_db_close(&db);
    assert(unlink(path) == 0);
    return 0;
}
