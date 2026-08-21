#ifndef IRCD_MEMOSERV_DB_H
#define IRCD_MEMOSERV_DB_H

/**
 * @file memoserv_db.h
 * @brief SQLite-backed account-to-account memo storage.
 */

#include <stddef.h>
#include <sqlite3.h>
#include "config.h"

/** Open MemoServ database handle. */
typedef struct MemoServDb {
    sqlite3 *handle;
} MemoServDb;

/** One persistent memo. */
typedef struct MemoServMemo {
    long long id;
    char sender[IRC_NICK_MAX + 1U];
    char recipient[IRC_NICK_MAX + 1U];
    char text[IRCD_MEMOSERV_TEXT_MAX + 1U];
    long long created_at;
    long long read_at;
} MemoServMemo;

int memoserv_db_open(MemoServDb *db, const char *path);
void memoserv_db_close(MemoServDb *db);

/** Store one memo and optionally return its generated id. */
int memoserv_db_send(MemoServDb *db, const char *sender,
                     const char *recipient, const char *text,
                     long long *memo_id);

/** Return unread count for one account. */
int memoserv_db_unread_count(MemoServDb *db, const char *recipient,
                             size_t *count);

/**
 * List newest memos for one account, newest first.
 * Returns 0 on success and writes the number copied to count.
 */
int memoserv_db_list(MemoServDb *db, const char *recipient,
                     MemoServMemo *memos, size_t capacity, size_t *count);

/** Fetch a memo owned by recipient without changing read state. */
int memoserv_db_get(MemoServDb *db, const char *recipient,
                    long long memo_id, MemoServMemo *memo);

/** Mark one recipient-owned memo read, setting read_at once. */
int memoserv_db_mark_read(MemoServDb *db, const char *recipient,
                          long long memo_id, long long when);

/** Delete one recipient-owned memo. Returns 1 if deleted, 0 if absent. */
int memoserv_db_delete(MemoServDb *db, const char *recipient,
                       long long memo_id);

/** Delete all memos for recipient. */
int memoserv_db_delete_all(MemoServDb *db, const char *recipient);

#endif /* IRCD_MEMOSERV_DB_H */
