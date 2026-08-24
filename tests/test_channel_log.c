#define _POSIX_C_SOURCE 200809L

#include "channel_log.h"
#include "chanserv_db.h"
#include "modes.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int read_file(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "r");
    size_t used;
    if (file == NULL || buffer == NULL || size == 0U) return -1;
    used = fread(buffer, 1U, size - 1U, file);
    buffer[used] = '\0';
    fclose(file);
    return 0;
}

int main(void) {
    char template_path[] = "/tmp/scratchircd-channel-log-XXXXXX";
    char *tmp = mkdtemp(template_path);
    char original[1024];
    char db_path[1200];
    char old_suffix[32];
    char new_suffix[32];
    char old_path[128];
    char new_path[128];
    char old_text[4096];
    char new_text[4096];
    char expected_boundary[128];
    Server server;
    Channel channel;
    Client client;
    ChanServDb db = {0};
    struct tm now_tm;
    struct tm next_tm;
    time_t now;
    time_t next_midnight;
    char *error = NULL;

    assert(tmp != NULL);
    assert(getcwd(original, sizeof(original)) != NULL);
    assert(chdir(tmp) == 0);

    memset(&server, 0, sizeof(server));
    memset(&channel, 0, sizeof(channel));
    memset(&client, 0, sizeof(client));
    (void)snprintf(db_path, sizeof(db_path), "%s/chanserv.db", tmp);
    (void)snprintf(server.config.chanserv_db, sizeof(server.config.chanserv_db), "%s", db_path);
    (void)snprintf(channel.name, sizeof(channel.name), "#Rotate");
    (void)snprintf(client.nick, sizeof(client.nick), "Alice");
    (void)snprintf(client.user, sizeof(client.user), "alice");
    (void)snprintf(client.display_host, sizeof(client.display_host), "cloak.example");

    assert(chanserv_db_open(&db, db_path) == 0);
    assert(chanserv_db_create(&db, channel.name, "Alice", "rotation test") == 0);
    assert(sqlite3_exec(db.db,
        "ALTER TABLE channels ADD COLUMN logging_enabled INTEGER NOT NULL DEFAULT 0;"
        "UPDATE channels SET logging_enabled=1 WHERE name='#Rotate';",
        NULL, NULL, &error) == SQLITE_OK);
    sqlite3_free(error);
    chanserv_db_close(&db);

    now = time(NULL);
    assert(localtime_r(&now, &now_tm) != NULL);
    (void)strftime(old_suffix, sizeof(old_suffix), "%d%b%Y", &now_tm);

    channel_log_message(&server, &channel, &client, "before midnight", 0);

    next_tm = now_tm;
    next_tm.tm_mday += 1;
    next_tm.tm_hour = 0;
    next_tm.tm_min = 0;
    next_tm.tm_sec = 0;
    next_tm.tm_isdst = -1;
    next_midnight = mktime(&next_tm);
    assert(next_midnight != (time_t)-1);
    assert(localtime_r(&next_midnight, &next_tm) != NULL);
    (void)strftime(new_suffix, sizeof(new_suffix), "%d%b%Y", &next_tm);

    channel_log_rotate_all(next_midnight);

    (void)snprintf(old_path, sizeof(old_path), "logs/Rotate.log.%s", old_suffix);
    (void)snprintf(new_path, sizeof(new_path), "logs/Rotate.log.%s", new_suffix);
    assert(read_file(old_path, old_text, sizeof(old_text)) == 0);
    assert(read_file(new_path, new_text, sizeof(new_text)) == 0);

    assert(strstr(old_text, "<Alice> before midnight") != NULL);
    assert(strstr(old_text, "[00:00:00] --- ") != NULL);
    (void)strftime(expected_boundary, sizeof(expected_boundary),
                   "[00:00:00] --- %B %d %Y 00:00:00.", &next_tm);
    assert(strstr(old_text, expected_boundary) != NULL);
    assert(strncmp(new_text, expected_boundary, strlen(expected_boundary)) == 0);

    assert(chdir(original) == 0);
    return 0;
}
