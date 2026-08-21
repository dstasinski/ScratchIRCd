#include "chanserv_db.h"
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