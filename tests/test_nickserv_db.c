#include "nickserv_db.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/scratchircd-nickserv-XXXXXX";
    int fd = mkstemp(path);
    NickServDb db = {0};
    NickServAccount account;
    NickServAccount loaded;
    NickServAccount second;
    NickServAccount third;

    assert(fd >= 0);
    close(fd);
    assert(unlink(path) == 0);

    assert(nickserv_db_open(&db, path) == 0);
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
    assert(nickserv_db_delete(&db, "Overflow") == 0);
    nickserv_db_close(&db);
    assert(unlink(path) == 0);
    return 0;
}
