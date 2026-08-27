#define _POSIX_C_SOURCE 200809L

#include "channel_log.h"
#include "chanserv_db.h"
#include "modes.h"
#include "oper.h"

#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* This unit test has no live IRC clients; it only needs to satisfy the
 * channel-log module's observability hook when exercising a full backlog. */
void snotice_broadcast(struct Server *server, SnoticeMask category,
                       const char *fmt, ...) {
    (void)server;
    (void)category;
    (void)fmt;
}

static int read_file(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "r");
    size_t used;
    if (file == NULL || buffer == NULL || size == 0U) return -1;
    used = fread(buffer, 1U, size - 1U, file);
    buffer[used] = '\0';
    fclose(file);
    return 0;
}

static size_t queue_count(ChanServDb *db) {
    size_t count = 0U;
    assert(chanserv_db_logging_queue_count(db, &count) == 0);
    return count;
}

static void create_log_with_mtime(const char *path, time_t when) {
    FILE *file;
    struct timespec times[2];
    file = fopen(path, "w");
    assert(file != NULL);
    assert(fputs("retention test\n", file) >= 0);
    assert(fclose(file) == 0);
    times[0].tv_sec = when;
    times[0].tv_nsec = 0;
    times[1] = times[0];
    assert(utimensat(AT_FDCWD, path, times, 0) == 0);
}

int main(void) {
    char template_path[] = "/tmp/scratchircd-channel-log-XXXXXX";
    char *tmp = mkdtemp(template_path);
    char original[1024];
    char db_path[1200];
    char old_suffix[32], new_suffix[32];
    char old_path[128], new_path[128];
    char old_text[4096], new_text[4096], expected_boundary[128];
    const char *expired_log = "logs/Expired.log.01Jan2000";
    const char *fresh_log = "logs/Fresh.log.01Jan2099";
    Server server;
    Channel channel;
    Client client;
    ChanServDb db = {0};
    ChanServLogQueueRecord rows[8];
    size_t fetched = 0U;
    struct tm now_tm, next_tm;
    time_t now, next_midnight;

    assert(tmp != NULL);
    assert(getcwd(original, sizeof(original)) != NULL);
    assert(chdir(tmp) == 0);

    memset(&server, 0, sizeof(server));
    memset(&channel, 0, sizeof(channel));
    memset(&client, 0, sizeof(client));
    server.config.channel_log_queue_max_rows = IRCD_DEFAULT_CHANNEL_LOG_QUEUE_MAX_ROWS;
    (void)snprintf(db_path, sizeof(db_path), "%s/chanserv.db", tmp);
    (void)snprintf(server.config.chanserv_db, sizeof(server.config.chanserv_db), "%s", db_path);
    (void)snprintf(channel.name, sizeof(channel.name), "#Rotate");
    (void)snprintf(client.nick, sizeof(client.nick), "Alice");
    (void)snprintf(client.user, sizeof(client.user), "alice");
    (void)snprintf(client.display_host, sizeof(client.display_host), "cloak.example");

    assert(chanserv_db_open(&db, db_path) == 0);
    assert(chanserv_db_create(&db, channel.name, "Alice", "rotation test") == 0);
    assert(chanserv_db_logging_set(&db, channel.name, 1) == 0);
    assert(queue_count(&db) == 0U);
    chanserv_db_close(&db);

    assert(channel_log_init(&server) == 0);
    now = time(NULL);
    assert(localtime_r(&now, &now_tm) != NULL);
    (void)strftime(old_suffix, sizeof(old_suffix), "%d%b%Y", &now_tm);
    (void)snprintf(old_path, sizeof(old_path), "logs/Rotate.log.%s", old_suffix);

    channel_log_message(&server, &channel, &client, "before midnight", 0);

    /* The event is durable immediately but not appended to the text log yet. */
    assert(chanserv_db_open(&db, db_path) == 0);
    assert(queue_count(&db) == 1U);
    chanserv_db_close(&db);
    assert(access(old_path, F_OK) != 0);

    /* At the configured ceiling, new events are refused, never old backlog. */
    server.config.channel_log_queue_max_rows = 1U;
    channel_log_message(&server, &channel, &client, "must not displace durable backlog", 0);
    assert(chanserv_db_open(&db, db_path) == 0);
    assert(queue_count(&db) == 1U);
    chanserv_db_close(&db);
    server.config.channel_log_queue_max_rows = IRCD_DEFAULT_CHANNEL_LOG_QUEUE_MAX_ROWS;

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

    (void)snprintf(new_path, sizeof(new_path), "logs/Rotate.log.%s", new_suffix);
    assert(read_file(old_path, old_text, sizeof(old_text)) == 0);
    assert(read_file(new_path, new_text, sizeof(new_text)) == 0);
    assert(strstr(old_text, "<Alice> before midnight") != NULL);
    assert(strstr(old_text, "must not displace durable backlog") == NULL);
    assert(strstr(old_text, "[00:00:00] --- ") != NULL);
    (void)strftime(expected_boundary, sizeof(expected_boundary),
                   "[00:00:00] --- %B %d %Y 00:00:00.", &next_tm);
    assert(strstr(old_text, expected_boundary) != NULL);
    assert(strncmp(new_text, expected_boundary, strlen(expected_boundary)) == 0);

    assert(chanserv_db_open(&db, db_path) == 0);
    assert(queue_count(&db) == 0U);

    /* Automatic paging is global by event age rather than channel name. */
    assert(chanserv_db_logging_queue_add(&db, "#Later", 300, "later") == 0);
    assert(chanserv_db_logging_queue_add(&db, "#Oldest", 100, "oldest") == 0);
    assert(chanserv_db_logging_queue_add(&db, "#Middle", 200, "middle") == 0);
    assert(chanserv_db_logging_queue_fetch_due(&db, 250, rows, 8, &fetched) == 0);
    assert(fetched == 2U);
    assert(strcmp(rows[0].channel, "#Oldest") == 0 && rows[0].event_time == 100);
    assert(strcmp(rows[1].channel, "#Middle") == 0 && rows[1].event_time == 200);
    assert(chanserv_db_logging_queue_delete_ordered_through(&db,
            rows[1].event_time, rows[1].id) == 0);
    assert(queue_count(&db) == 1U);
    assert(chanserv_db_logging_queue_fetch_due(&db, 1000, rows, 8, &fetched) == 0);
    assert(fetched == 1U && strcmp(rows[0].channel, "#Later") == 0);
    assert(chanserv_db_logging_queue_delete_ordered_through(&db,
            rows[0].event_time, rows[0].id) == 0);

    /* Per-channel administrative drains use insertion order so id<=last_id
     * deletion remains safe even when the wall clock moves backward. */
    assert(chanserv_db_logging_queue_add(&db, "#Clock", 500, "first inserted") == 0);
    assert(chanserv_db_logging_queue_add(&db, "#Clock", 400, "second inserted") == 0);
    assert(chanserv_db_logging_queue_fetch(&db, "#Clock", rows, 8, &fetched) == 0);
    assert(fetched == 2U);
    assert(strcmp(rows[0].body, "first inserted") == 0 && rows[0].event_time == 500);
    assert(strcmp(rows[1].body, "second inserted") == 0 && rows[1].event_time == 400);
    assert(chanserv_db_logging_queue_delete_through(&db, "#Clock", rows[1].id) == 0);
    assert(queue_count(&db) == 0U);
    chanserv_db_close(&db);

    /* Retention is based on actual file age, not trust in a filename date. */
    create_log_with_mtime(expired_log,
                          next_midnight - (time_t)(IRCD_CHANNEL_LOG_RETENTION_DAYS + 1U) * 86400);
    create_log_with_mtime(fresh_log, next_midnight - 3600);
    assert(access(expired_log, F_OK) == 0);
    assert(access(fresh_log, F_OK) == 0);
    channel_log_rotate_all(next_midnight + 3600);
    assert(access(expired_log, F_OK) != 0);
    assert(access(fresh_log, F_OK) == 0);

    assert(chdir(original) == 0);
    return 0;
}