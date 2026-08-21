#include "chanserv_db.h"
#include "modes.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/scratchircd-chanserv-XXXXXX";
    int fd = mkstemp(path);
    ChanServDb db = {0};
    ChanServChannel record;
    ChanServAccess access;
    char list[256];

    assert(fd >= 0);
    close(fd);
    unlink(path);

    assert(chanserv_db_open(&db, path) == 0);
    assert(chanserv_db_create(&db, "#Test", "Alice", "Example channel") == 0);
    assert(chanserv_db_get(&db, "#test", &record) == 1);
    assert(strcmp(record.name, "#Test") == 0);
    assert(strcmp(record.founder, "Alice") == 0);
    assert(strcmp(record.description, "Example channel") == 0);
    assert(record.enabled == 1);
    assert(record.mode_lock == 0U);
    assert(record.topic[0] == '\0');

    assert(chanserv_db_set_mode_lock(&db, "#test",
           CHANNEL_MODE_NO_EXTERNAL | CHANNEL_MODE_TOPIC_LOCK) == 0);
    assert(chanserv_db_set_topic(&db, "#test", "Persistent topic",
                                  "Alice!alice@example", 12345) == 0);
    assert(chanserv_db_get(&db, "#TEST", &record) == 1);
    assert((record.mode_lock & CHANNEL_MODE_NO_EXTERNAL) != 0U);
    assert((record.mode_lock & CHANNEL_MODE_TOPIC_LOCK) != 0U);
    assert(strcmp(record.topic, "Persistent topic") == 0);
    assert(strcmp(record.topic_setter, "Alice!alice@example") == 0);
    assert(record.topic_time == 12345);

    assert(chanserv_db_access_set(&db, "#TEST", "Bob", CHANSERV_ACCESS_OP) == 0);
    assert(chanserv_db_access_get(&db, "#test", "bob", &access) == 1);
    assert(strcmp(access.account, "Bob") == 0);
    assert(access.level == CHANSERV_ACCESS_OP);

    assert(chanserv_db_access_set(&db, "#TEST", "Carol",
                                  CHANSERV_ACCESS_PROTECTED) == 0);
    assert(chanserv_db_access_get(&db, "#test", "carol", &access) == 1);
    assert(access.level == CHANSERV_ACCESS_PROTECTED);

    assert(chanserv_db_access_list(&db, "#test", list, sizeof(list)) == 0);
    assert(strstr(list, "Bob:3") != NULL);
    assert(strstr(list, "Carol:4") != NULL);

    assert(chanserv_db_access_set(&db, "#test", "Bob", CHANSERV_ACCESS_VOICE) == 0);
    assert(chanserv_db_access_get(&db, "#TEST", "BOB", &access) == 1);
    assert(access.level == CHANSERV_ACCESS_VOICE);
    assert(chanserv_db_access_delete(&db, "#test", "bob") == 0);
    assert(chanserv_db_access_delete(&db, "#test", "carol") == 0);
    assert(chanserv_db_access_get(&db, "#test", "Bob", &access) == 0);

    assert(chanserv_db_list_enabled(&db, list, sizeof(list)) == 0);
    assert(strstr(list, "#Test") != NULL);

    assert(chanserv_db_set_description(&db, "#TEST", "Changed") == 0);
    assert(chanserv_db_set_founder(&db, "#test", "Bob") == 0);
    assert(chanserv_db_set_enabled(&db, "#test", 0) == 0);
    assert(chanserv_db_get(&db, "#test", &record) == 1);
    assert(strcmp(record.founder, "Bob") == 0);
    assert(strcmp(record.description, "Changed") == 0);
    assert(record.enabled == 0);

    assert(chanserv_db_list_enabled(&db, list, sizeof(list)) == 0);
    assert(list[0] == '\0');

    assert(chanserv_db_delete(&db, "#test") == 0);
    assert(chanserv_db_get(&db, "#test", &record) == 0);

    /* SQLite persistence must use the same RFC1459 casemapping as hashes. */
    assert(chanserv_db_create(&db, "#[Fold]", "Alice", "RFC1459") == 0);
    assert(chanserv_db_get(&db, "#{fOLD}", &record) == 1);
    assert(strcmp(record.name, "#[Fold]") == 0);
    assert(chanserv_db_delete(&db, "#{fold}") == 0);

    chanserv_db_close(&db);
    unlink(path);
    puts("chanserv db tests passed");
    return 0;
}
