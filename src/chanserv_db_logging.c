/**
 * @file chanserv_db_logging.c
 * @brief ChanServ persistence for optional per-channel logging.
 */

#include "chanserv_db.h"

#include <stdio.h>
#include <string.h>

static int column_exists(sqlite3 *db, const char *column) {
    sqlite3_stmt *stmt = NULL;
    int found = 0;

    if (db == NULL || column == NULL) return 0;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(channels)", -1,
                           &stmt, NULL) != SQLITE_OK)
        return 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name != NULL && strcmp(name, column) == 0) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

int chanserv_db_logging_ensure_schema(ChanServDb *db) {
    char *error = NULL;
    int rc;

    if (db == NULL || db->db == NULL) return -1;
    if (!column_exists(db->db, "logging_enabled")) {
        rc = sqlite3_exec(db->db,
            "ALTER TABLE channels ADD COLUMN logging_enabled INTEGER NOT NULL DEFAULT 0",
            NULL, NULL, &error);
        if (rc != SQLITE_OK) {
            if (error != NULL)
                fprintf(stderr, "ChanServ DB logging migration: %s\n", error);
            sqlite3_free(error);
            return -1;
        }
    }

    error = NULL;
    rc = sqlite3_exec(db->db,
        "CREATE TABLE IF NOT EXISTS channel_log_queue ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "channel TEXT COLLATE IRCNOCASE NOT NULL,"
        "event_time INTEGER NOT NULL,"
        "body TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS channel_log_queue_channel_time "
        "ON channel_log_queue(channel,event_time,id);"
        "CREATE INDEX IF NOT EXISTS channel_log_queue_time "
        "ON channel_log_queue(event_time,id);",
        NULL, NULL, &error);
    if (rc != SQLITE_OK) {
        if (error != NULL)
            fprintf(stderr, "ChanServ DB logging queue schema: %s\n", error);
        sqlite3_free(error);
        return -1;
    }
    return 0;
}

int chanserv_db_logging_get(ChanServDb *db, const char *name,
                            int *registered, int *enabled) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (registered != NULL) *registered = 0;
    if (enabled != NULL) *enabled = 0;
    if (db == NULL || db->db == NULL || name == NULL) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;

    if (sqlite3_prepare_v2(db->db,
        "SELECT enabled,logging_enabled FROM channels WHERE name=?1",
        -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int channel_enabled = sqlite3_column_int(stmt, 0) != 0;
        if (registered != NULL) *registered = channel_enabled;
        if (enabled != NULL)
            *enabled = channel_enabled && sqlite3_column_int(stmt, 1) != 0;
        sqlite3_finalize(stmt);
        return 0;
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_db_logging_set(ChanServDb *db, const char *name, int enabled) {
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (db == NULL || db->db == NULL || name == NULL) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;

    if (sqlite3_prepare_v2(db->db,
        "UPDATE channels SET logging_enabled=?1,updated_at=unixepoch() "
        "WHERE name=?2 AND enabled=1",
        -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE && sqlite3_changes(db->db) > 0 ? 0 : -1;
}

int chanserv_db_logging_queue_add(ChanServDb *db, const char *channel,
                                  long long event_time, const char *body) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || channel == NULL || body == NULL) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->db,
        "INSERT INTO channel_log_queue(channel,event_time,body) VALUES(?1,?2,?3)",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, channel, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)event_time);
    sqlite3_bind_text(stmt, 3, body, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_db_logging_queue_count(ChanServDb *db, size_t *count) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (count != NULL) *count = 0U;
    if (db == NULL || db->db == NULL || count == NULL) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->db, "SELECT COUNT(*) FROM channel_log_queue",
                           -1, &stmt, NULL) != SQLITE_OK) return -1;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) *count = (size_t)sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 0 : -1;
}

int chanserv_db_logging_queue_oldest(ChanServDb *db, long long *event_time) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (event_time != NULL) *event_time = 0;
    if (db == NULL || db->db == NULL || event_time == NULL) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->db,
        "SELECT MIN(event_time) FROM channel_log_queue", -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL)
        *event_time = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 0 : -1;
}

static void queue_record_from_stmt(sqlite3_stmt *stmt,
                                   ChanServLogQueueRecord *record) {
    const unsigned char *value;
    memset(record, 0, sizeof(*record));
    record->id = sqlite3_column_int64(stmt, 0);
    value = sqlite3_column_text(stmt, 1);
    (void)snprintf(record->channel, sizeof(record->channel), "%s",
                   value != NULL ? (const char *)value : "");
    record->event_time = sqlite3_column_int64(stmt, 2);
    value = sqlite3_column_text(stmt, 3);
    (void)snprintf(record->body, sizeof(record->body), "%s",
                   value != NULL ? (const char *)value : "");
}

int chanserv_db_logging_queue_fetch(ChanServDb *db, const char *channel,
                                    ChanServLogQueueRecord *records,
                                    size_t capacity, size_t *count) {
    sqlite3_stmt *stmt = NULL;
    size_t used = 0U;
    int rc;
    if (count != NULL) *count = 0U;
    if (db == NULL || db->db == NULL || channel == NULL || records == NULL ||
        capacity == 0U) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->db,
        "SELECT id,channel,event_time,body FROM channel_log_queue "
        "WHERE channel=?1 ORDER BY event_time,id LIMIT ?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, channel, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)capacity);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && used < capacity)
        queue_record_from_stmt(stmt, &records[used++]);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    if (count != NULL) *count = used;
    return 0;
}

int chanserv_db_logging_queue_fetch_due(ChanServDb *db, long long cutoff,
                                        ChanServLogQueueRecord *records,
                                        size_t capacity, size_t *count) {
    sqlite3_stmt *stmt = NULL;
    size_t used = 0U;
    int rc;
    if (count != NULL) *count = 0U;
    if (db == NULL || db->db == NULL || records == NULL || capacity == 0U) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->db,
        "SELECT id,channel,event_time,body FROM channel_log_queue "
        "WHERE event_time<=?1 ORDER BY event_time,id LIMIT ?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int64(stmt, 1, cutoff);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)capacity);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && used < capacity)
        queue_record_from_stmt(stmt, &records[used++]);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    if (count != NULL) *count = used;
    return 0;
}

int chanserv_db_logging_queue_delete_ordered_through(ChanServDb *db,
                                                      long long event_time,
                                                      long long id) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || id <= 0) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->db,
        "DELETE FROM channel_log_queue WHERE event_time<?1 "
        "OR (event_time=?1 AND id<=?2)", -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int64(stmt, 1, event_time);
    sqlite3_bind_int64(stmt, 2, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_db_logging_queue_list_channels(ChanServDb *db, char *buffer,
                                            size_t size) {
    sqlite3_stmt *stmt = NULL;
    size_t used = 0U;
    int rc;
    if (db == NULL || db->db == NULL || buffer == NULL || size == 0U) return -1;
    buffer[0] = '\0';
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->db,
        "SELECT DISTINCT channel FROM channel_log_queue ORDER BY channel COLLATE IRCNOCASE",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 0);
        size_t length = name != NULL ? strlen(name) : 0U;
        if (length == 0U) continue;
        if (used != 0U) {
            if (used + 1U >= size) break;
            buffer[used++] = ',';
        }
        if (length >= size - used) break;
        memcpy(buffer + used, name, length);
        used += length;
        buffer[used] = '\0';
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE || rc == SQLITE_ROW ? 0 : -1;
}

int chanserv_db_logging_queue_delete_through(ChanServDb *db, const char *channel,
                                             long long id) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || channel == NULL) return -1;
    if (chanserv_db_logging_ensure_schema(db) != 0) return -1;
    if (sqlite3_prepare_v2(db->db,
        "DELETE FROM channel_log_queue WHERE channel=?1 AND id<=?2",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, channel, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}