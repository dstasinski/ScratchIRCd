#include "chanserv_db.h"
#include "chanserv_persist.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/scratchircd-chanpersist-XXXXXX";
    int fd = mkstemp(path);
    ChanServDb db = {0};
    Channel source;
    Channel restored;

    assert(fd >= 0);
    close(fd);
    unlink(path);

    assert(chanserv_db_open(&db, path) == 0);
    assert(chanserv_db_create(&db, "#Persist", "Alice", "runtime state") == 0);
    chanserv_db_close(&db);

    memset(&source, 0, sizeof(source));
    snprintf(source.name, sizeof(source.name), "%s", "#Persist");
    snprintf(source.key, sizeof(source.key), "%s", "secret");
    source.user_limit = 25U;
    source.join_throttle_count = 3U;
    source.join_throttle_seconds = 30U;
    snprintf(source.limit_redirect, sizeof(source.limit_redirect), "%s", "#overflow");
    snprintf(source.ban_redirect, sizeof(source.ban_redirect), "%s", "#banned");
    assert(channel_mask_add_authorized(&source.ban_list, "Bad!*@*", 1) == 0);
    assert(channel_mask_add(&source.exception_list, "Friend!*@*") == 0);
    assert(channel_mask_add(&source.invite_exception_list, "Invite!*@*") == 0);

    assert(chanserv_persist_save(path, &source) == 0);

    memset(&restored, 0, sizeof(restored));
    snprintf(restored.name, sizeof(restored.name), "%s", "#persist");
    assert(chanserv_persist_restore(path, &restored) == 0);
    assert(strcmp(restored.key, "secret") == 0);
    assert(restored.user_limit == 25U);
    assert(restored.join_throttle_count == 3U);
    assert(restored.join_throttle_seconds == 30U);
    assert(strcmp(restored.limit_redirect, "#overflow") == 0);
    assert(strcmp(restored.ban_redirect, "#banned") == 0);
    assert(restored.ban_list != NULL);
    assert(strcmp(restored.ban_list->mask, "Bad!*@*") == 0);
    assert(restored.ban_list->protected_authorized == 1);
    assert(restored.exception_list != NULL);
    assert(strcmp(restored.exception_list->mask, "Friend!*@*") == 0);
    assert(restored.invite_exception_list != NULL);
    assert(strcmp(restored.invite_exception_list->mask, "Invite!*@*") == 0);

    channel_mask_clear(&source.ban_list);
    channel_mask_clear(&source.exception_list);
    channel_mask_clear(&source.invite_exception_list);
    channel_mask_clear(&restored.ban_list);
    channel_mask_clear(&restored.exception_list);
    channel_mask_clear(&restored.invite_exception_list);
    unlink(path);
    puts("chanserv persistence tests passed");
    return 0;
}
