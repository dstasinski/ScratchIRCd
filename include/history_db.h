#ifndef IRCD_HISTORY_DB_H
#define IRCD_HISTORY_DB_H

#include <stddef.h>
#include <stdint.h>
#include <sqlite3.h>

#include "config.h"

/** One persisted channel PRIVMSG/NOTICE record. */
typedef struct HistoryRecord {
    int64_t id;
    char target[IRC_CHANNEL_NAME_MAX + 1U];
    char command[IRCD_HISTORY_COMMAND_MAX + 1U];
    char nick[IRC_NICK_MAX + 1U];
    char user[IRC_USER_MAX + 1U];
    char host[IRC_HOST_MAX + 1U];
    char account[IRC_NICK_MAX + 1U];
    char text[IRCD_HISTORY_TEXT_MAX + 1U];
    int64_t created_at_ms;
} HistoryRecord;

typedef struct HistoryDb {
    sqlite3 *handle;
} HistoryDb;

int history_db_open(HistoryDb *db, const char *path);
void history_db_close(HistoryDb *db);
int history_db_add(HistoryDb *db, const HistoryRecord *record);
int history_db_latest(HistoryDb *db, const char *target, size_t limit,
                      HistoryRecord *records, size_t capacity, size_t *count);

/**
 * Return a process-reused history handle for path. The handle is opened lazily,
 * retained across commands, and automatically replaced if a restart selects a
 * different history_db path. A failed open is retried by the next caller.
 */
HistoryDb *history_db_shared(const char *path);

#endif /* IRCD_HISTORY_DB_H */
