#ifndef IRCD_MEMOSERV_DB_H
#define IRCD_MEMOSERV_DB_H

/**
 * @file memoserv_db.h
 * @brief SQLite-backed account-to-account memo storage.
 */

#include <stddef.h>
#include <sqlite3.h>
#include "config.h"

typedef struct MemoServDb {
    sqlite3 *handle;
} MemoServDb;

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
int memoserv_db_send(MemoServDb *db, const char *sender,
                     const char *recipient, const char *text,
                     long long *memo_id);
int memoserv_db_unread_count(MemoServDb *db, const char *recipient,
                             size_t *count);
int memoserv_db_count(MemoServDb *db, const char *recipient, size_t *count);
int memoserv_db_list(MemoServDb *db, const char *recipient,
                     MemoServMemo *memos, size_t capacity, size_t *count);
int memoserv_db_list_sent(MemoServDb *db, const char *sender,
                          MemoServMemo *memos, size_t capacity, size_t *count);
int memoserv_db_get(MemoServDb *db, const char *recipient,
                    long long memo_id, MemoServMemo *memo);
int memoserv_db_get_sent(MemoServDb *db, const char *sender,
                         long long memo_id, MemoServMemo *memo);
int memoserv_db_mark_read(MemoServDb *db, const char *recipient,
                          long long memo_id, long long when);
int memoserv_db_delete(MemoServDb *db, const char *recipient,
                       long long memo_id);
int memoserv_db_delete_all(MemoServDb *db, const char *recipient);

/** Delete memos older than cutoff; recipient may be NULL to purge globally. */
int memoserv_db_purge_before(MemoServDb *db, const char *recipient,
                             long long cutoff, size_t *deleted);

#endif /* IRCD_MEMOSERV_DB_H */
