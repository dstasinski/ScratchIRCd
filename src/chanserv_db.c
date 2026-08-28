/** @file chanserv_db.c @brief SQLite persistence for registered channels. */
#include "chanserv_db.h"
#include "sqlite_policy.h"
#include <stdio.h>
#include <string.h>

#define CHANSERV_DB_SCHEMA_VERSION 1

static uint64_t pchannels_generation = 1U;

uint64_t chanserv_db_pchannels_generation(void) {
    return pchannels_generation;
}

static void pchannels_changed(void) {
    if (++pchannels_generation == 0U) pchannels_generation = 1U;
}

static unsigned char irc_fold(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return (unsigned char)(ch + ('a' - 'A'));
    switch (ch) {
        case '{': return '[';
        case '}': return ']';
        case '|': return '\\';
        case '~': return '^';
        default: return ch;
    }
}

static int irc_collation(void *context, int left_len, const void *left_data,
                         int right_len, const void *right_data) {
    const unsigned char *left = left_data;
    const unsigned char *right = right_data;
    int length = left_len < right_len ? left_len : right_len;
    int i;
    (void)context;
    for (i = 0; i < length; ++i) {
        unsigned char a = irc_fold(left[i]);
        unsigned char b = irc_fold(right[i]);
        if (a < b) return -1;
        if (a > b) return 1;
    }
    return left_len < right_len ? -1 : left_len > right_len ? 1 : 0;
}

static int exec_sql(sqlite3 *db, const char *sql) {
    char *error = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &error);
    if (rc != SQLITE_OK) {
        if (error != NULL) fprintf(stderr, "ChanServ DB: %s\n", error);
        sqlite3_free(error);
        return -1;
    }
    return 0;
}

/** Copy a SQLite TEXT column losslessly into a fixed IRC structure field. */
static int copy_text_column(sqlite3_stmt *stmt, int column,
                            char *destination, size_t destination_size) {
    const unsigned char *text;
    int bytes;
    if (stmt == NULL || destination == NULL || destination_size == 0U) return -1;
    text = sqlite3_column_text(stmt, column);
    bytes = sqlite3_column_bytes(stmt, column);
    if (text == NULL || bytes < 0 || (size_t)bytes >= destination_size) return -1;
    if (bytes != 0 && memchr(text, '\0', (size_t)bytes) != NULL) return -1;
    memcpy(destination, text, (size_t)bytes);
    destination[bytes] = '\0';
    return 0;
}

static int text_arg_fits(const char *text, size_t maximum, int allow_empty) {
    size_t length;
    if (text == NULL) return 0;
    length = strlen(text);
    if ((!allow_empty && length == 0U) || length > maximum) return 0;
    return 1;
}

static int channel_arg_fits(const char *name) {
    return text_arg_fits(name, IRC_CHANNEL_NAME_MAX, 0);
}

static int account_arg_fits(const char *account) {
    return text_arg_fits(account, IRC_NICK_MAX, 0);
}

static int column_exists(sqlite3 *db, const char *column) {
    sqlite3_stmt *stmt = NULL;
    int found = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(channels)", -1, &stmt, NULL) != SQLITE_OK)
        return 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name != NULL && strcmp(name, column) == 0) { found = 1; break; }
    }
    sqlite3_finalize(stmt);
    return found;
}

static int ensure_channel_columns(sqlite3 *db) {
    if (!column_exists(db, "mode_lock") &&
        exec_sql(db, "ALTER TABLE channels ADD COLUMN mode_lock INTEGER NOT NULL DEFAULT 0") != 0)
        return -1;
    if (!column_exists(db, "topic") &&
        exec_sql(db, "ALTER TABLE channels ADD COLUMN topic TEXT NOT NULL DEFAULT ''") != 0)
        return -1;
    if (!column_exists(db, "topic_setter") &&
        exec_sql(db, "ALTER TABLE channels ADD COLUMN topic_setter TEXT NOT NULL DEFAULT ''") != 0)
        return -1;
    if (!column_exists(db, "topic_time") &&
        exec_sql(db, "ALTER TABLE channels ADD COLUMN topic_time INTEGER NOT NULL DEFAULT 0") != 0)
        return -1;
    return 0;
}

static int access_table_supports_protected(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int supported = 0;
    if (sqlite3_prepare_v2(db,
        "SELECT sql FROM sqlite_master WHERE type='table' AND name='access'",
        -1, &stmt, NULL) != SQLITE_OK) return 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *sql = (const char *)sqlite3_column_text(stmt, 0);
        if (sql != NULL && strstr(sql, "BETWEEN 1 AND 5") != NULL) supported = 1;
    }
    sqlite3_finalize(stmt);
    return supported;
}

static int ensure_access_schema(sqlite3 *db) {
    static const char migration[] =
        "BEGIN IMMEDIATE;"
        "CREATE TABLE access_new ("
        "channel TEXT COLLATE IRCNOCASE NOT NULL,"
        "account TEXT COLLATE NOCASE NOT NULL,"
        "level INTEGER NOT NULL CHECK(level BETWEEN 1 AND 5),"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "updated_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "PRIMARY KEY(channel,account),"
        "FOREIGN KEY(channel) REFERENCES channels(name) ON DELETE CASCADE"
        ");"
        "INSERT INTO access_new(channel,account,level,created_at,updated_at) "
        "SELECT channel,account,level,created_at,updated_at FROM access;"
        "DROP TABLE access;"
        "ALTER TABLE access_new RENAME TO access;"
        "COMMIT;";
    if (access_table_supports_protected(db)) return 0;
    return exec_sql(db, migration);
}

static int schema_version(sqlite3 *db, int *version) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || version == NULL) return -1;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) *version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW ? 0 : -1;
}

static int ensure_schema_version(sqlite3 *db) {
    int version = 0;
    if (schema_version(db, &version) != 0) return -1;
    if (version > CHANSERV_DB_SCHEMA_VERSION) {
        fprintf(stderr, "ChanServ DB: unsupported schema version %d (server supports %d)\n",
                version, CHANSERV_DB_SCHEMA_VERSION);
        return -1;
    }
    if (version == CHANSERV_DB_SCHEMA_VERSION) return 0;
    if (ensure_channel_columns(db) != 0 || ensure_access_schema(db) != 0)
        return -1;
    return exec_sql(db, "PRAGMA user_version=1;");
}

int chanserv_db_open(ChanServDb *db, const char *path) {
    static const char schema[] =
        "CREATE TABLE IF NOT EXISTS channels ("
        "name TEXT COLLATE IRCNOCASE PRIMARY KEY,"
        "founder TEXT COLLATE NOCASE NOT NULL,"
        "description TEXT NOT NULL DEFAULT '',"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "mode_lock INTEGER NOT NULL DEFAULT 0,"
        "topic TEXT NOT NULL DEFAULT '',"
        "topic_setter TEXT NOT NULL DEFAULT '',"
        "topic_time INTEGER NOT NULL DEFAULT 0,"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "updated_at INTEGER NOT NULL DEFAULT (unixepoch())"
        ");"
        "CREATE INDEX IF NOT EXISTS channels_founder_idx ON channels(founder);"
        "CREATE INDEX IF NOT EXISTS channels_enabled_name_idx ON channels(enabled,name COLLATE IRCNOCASE);"
        "CREATE TABLE IF NOT EXISTS access ("
        "channel TEXT COLLATE IRCNOCASE NOT NULL,"
        "account TEXT COLLATE NOCASE NOT NULL,"
        "level INTEGER NOT NULL CHECK(level BETWEEN 1 AND 5),"
        "created_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "updated_at INTEGER NOT NULL DEFAULT (unixepoch()),"
        "PRIMARY KEY(channel,account),"
        "FOREIGN KEY(channel) REFERENCES channels(name) ON DELETE CASCADE"
        ");";
    if (db == NULL || path == NULL) return -1;
    memset(db, 0, sizeof(*db));
    if (sqlite3_open(path, &db->db) != SQLITE_OK) { chanserv_db_close(db); return -1; }
    if (ircd_sqlite_apply_policy(db->db) != 0) { chanserv_db_close(db); return -1; }
    if (sqlite3_create_collation(db->db, "IRCNOCASE", SQLITE_UTF8, NULL, irc_collation) != SQLITE_OK) {
        chanserv_db_close(db); return -1;
    }
    if (exec_sql(db->db, "PRAGMA foreign_keys=ON;") != 0 ||
        exec_sql(db->db, schema) != 0 ||
        ensure_schema_version(db->db) != 0) {
        chanserv_db_close(db); return -1;
    }
    return 0;
}

void chanserv_db_close(ChanServDb *db) {
    if (db != NULL && db->db != NULL) sqlite3_close(db->db);
    if (db != NULL) db->db = NULL;
}

int chanserv_db_get(ChanServDb *db, const char *name, ChanServChannel *record) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || !channel_arg_fits(name) || record == NULL) return -1;
    memset(record, 0, sizeof(*record));
    if (sqlite3_prepare_v2(db->db,
        "SELECT name,founder,description,enabled,mode_lock,topic,topic_setter,topic_time,created_at,updated_at FROM channels WHERE name=?1",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        if (copy_text_column(stmt, 0, record->name, sizeof(record->name)) != 0 ||
            copy_text_column(stmt, 1, record->founder, sizeof(record->founder)) != 0 ||
            copy_text_column(stmt, 2, record->description, sizeof(record->description)) != 0 ||
            copy_text_column(stmt, 5, record->topic, sizeof(record->topic)) != 0 ||
            copy_text_column(stmt, 6, record->topic_setter, sizeof(record->topic_setter)) != 0) {
            sqlite3_finalize(stmt);
            memset(record, 0, sizeof(*record));
            return -1;
        }
        record->enabled = sqlite3_column_int(stmt, 3);
        record->mode_lock = (uint64_t)sqlite3_column_int64(stmt, 4);
        record->topic_time = sqlite3_column_int64(stmt, 7);
        record->created_at = sqlite3_column_int64(stmt, 8);
        record->updated_at = sqlite3_column_int64(stmt, 9);
        sqlite3_finalize(stmt);
        return 1;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_db_create(ChanServDb *db, const char *name, const char *founder, const char *description) {
    sqlite3_stmt *stmt = NULL;
    const char *stored_description = description != NULL ? description : "";
    int rc, changed;
    if (db == NULL || db->db == NULL || !channel_arg_fits(name) ||
        !account_arg_fits(founder) ||
        !text_arg_fits(stored_description, IRCD_CHANSERV_DESCRIPTION_MAX, 1)) return -1;
    if (sqlite3_prepare_v2(db->db,
        "INSERT INTO channels(name,founder,description) "
        "SELECT ?1,?2,?3 WHERE (SELECT COUNT(*) FROM channels) < ?4",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt,1,name,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,2,founder,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,3,stored_description,-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,4,(sqlite3_int64)IRCD_CHANSERV_REGISTRATION_HARD_MAX);
    rc=sqlite3_step(stmt);
    changed=sqlite3_changes(db->db);
    sqlite3_finalize(stmt);
    if(rc==SQLITE_DONE && changed>0){pchannels_changed();return 0;}
    return -1;
}

static int update_text(ChanServDb *db, const char *name, const char *column, const char *value) {
    char sql[192]; sqlite3_stmt *stmt=NULL; int rc;
    if (db==NULL || db->db==NULL || !channel_arg_fits(name) || value==NULL) return -1;
    (void)snprintf(sql,sizeof(sql),"UPDATE channels SET %s=?1,updated_at=unixepoch() WHERE name=?2",column);
    if(sqlite3_prepare_v2(db->db,sql,-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,value,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,name,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE && sqlite3_changes(db->db)>0 ? 0 : -1;
}

int chanserv_db_set_description(ChanServDb *db,const char *name,const char *description){
    if(!text_arg_fits(description,IRCD_CHANSERV_DESCRIPTION_MAX,1))return -1;
    return update_text(db,name,"description",description);
}
int chanserv_db_set_founder(ChanServDb *db,const char *name,const char *founder){
    if(!account_arg_fits(founder))return -1;
    return update_text(db,name,"founder",founder);
}

int chanserv_db_set_enabled(ChanServDb *db, const char *name, int enabled) {
    sqlite3_stmt *stmt=NULL;
    int rc, changed;
    if(db==NULL||db->db==NULL||!channel_arg_fits(name))return -1;
    if(sqlite3_prepare_v2(db->db,"UPDATE channels SET enabled=?1,updated_at=unixepoch() WHERE name=?2",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_int(stmt,1,enabled?1:0); sqlite3_bind_text(stmt,2,name,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); changed=sqlite3_changes(db->db); sqlite3_finalize(stmt);
    if(rc==SQLITE_DONE && changed>0){pchannels_changed();return 0;}
    return -1;
}

int chanserv_db_set_mode_lock(ChanServDb *db, const char *name, uint64_t mode_lock) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||!channel_arg_fits(name))return -1;
    if(sqlite3_prepare_v2(db->db,"UPDATE channels SET mode_lock=?1,updated_at=unixepoch() WHERE name=?2",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_int64(stmt,1,(sqlite3_int64)mode_lock); sqlite3_bind_text(stmt,2,name,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE && sqlite3_changes(db->db)>0 ? 0 : -1;
}

int chanserv_db_set_topic(ChanServDb *db, const char *name, const char *topic,
                          const char *setter, long long topic_time) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||!channel_arg_fits(name)||
       !text_arg_fits(topic,IRC_CHANNEL_TOPIC_MAX,1)||
       !text_arg_fits(setter,IRC_CHANNEL_TOPIC_SETTER_MAX,1))return -1;
    if(sqlite3_prepare_v2(db->db,
        "UPDATE channels SET topic=?1,topic_setter=?2,topic_time=?3,updated_at=unixepoch() WHERE name=?4",
        -1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,topic,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,setter,-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,3,(sqlite3_int64)topic_time); sqlite3_bind_text(stmt,4,name,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE && sqlite3_changes(db->db)>0 ? 0 : -1;
}

int chanserv_db_delete(ChanServDb *db, const char *name) {
    sqlite3_stmt *stmt=NULL;
    int rc, changed;
    if(db==NULL||db->db==NULL||!channel_arg_fits(name))return -1;
    if(sqlite3_prepare_v2(db->db,"DELETE FROM channels WHERE name=?1",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,name,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); changed=sqlite3_changes(db->db); sqlite3_finalize(stmt);
    if(rc==SQLITE_DONE && changed>0){pchannels_changed();return 0;}
    return -1;
}

int chanserv_db_list_enabled(ChanServDb *db, char *buffer, size_t size) {
    sqlite3_stmt *stmt = NULL;
    size_t used = 0U;
    int rc;
    int truncated = 0;
    if (db == NULL || db->db == NULL || buffer == NULL || size == 0U) return -1;
    buffer[0] = '\0';
    if (sqlite3_prepare_v2(db->db,
        "SELECT name FROM channels WHERE enabled=1 ORDER BY name COLLATE IRCNOCASE",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const unsigned char *raw = sqlite3_column_text(stmt, 0);
        int bytes = sqlite3_column_bytes(stmt, 0);
        const char *name = (const char *)raw;
        size_t n;
        size_t need;
        if(raw==NULL||bytes<=0)continue;
        n=(size_t)bytes;
        if(n>IRC_CHANNEL_NAME_MAX||strlen(name)!=n){truncated=1;break;}
        need = n + (used != 0U ? 1U : 0U);
        if (need >= size - used) {
            truncated = 1;
            break;
        }
        if (used != 0U) buffer[used++] = ',';
        memcpy(buffer + used, name, n);
        used += n;
        buffer[used] = '\0';
    }
    sqlite3_finalize(stmt);
    if (truncated) return -1;
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_db_access_set(ChanServDb *db, const char *channel, const char *account,
                           ChanServAccessLevel level) {
    sqlite3_stmt *stmt=NULL; int rc, changed;
    if(db==NULL||db->db==NULL||!channel_arg_fits(channel)||!account_arg_fits(account)||
       level<CHANSERV_ACCESS_VOICE||
       (level>CHANSERV_ACCESS_OWNER && level!=CHANSERV_ACCESS_PROTECTED)) return -1;
    if(sqlite3_prepare_v2(db->db,
        "INSERT INTO access(channel,account,level) "
        "SELECT ?1,?2,?3 WHERE EXISTS(SELECT 1 FROM access WHERE channel=?1 AND account=?2) "
        "OR (SELECT COUNT(*) FROM access WHERE channel=?1) < ?4 "
        "ON CONFLICT(channel,account) DO UPDATE SET level=excluded.level,updated_at=unixepoch()",
        -1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,channel,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,2,account,-1,SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt,3,(int)level);
    sqlite3_bind_int64(stmt,4,(sqlite3_int64)IRCD_CHANSERV_ACCESS_HARD_MAX);
    rc=sqlite3_step(stmt); changed=sqlite3_changes(db->db); sqlite3_finalize(stmt);
    return rc==SQLITE_DONE && changed>0?0:-1;
}

int chanserv_db_access_delete(ChanServDb *db, const char *channel, const char *account) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||!channel_arg_fits(channel)||!account_arg_fits(account))return -1;
    if(sqlite3_prepare_v2(db->db,"DELETE FROM access WHERE channel=?1 AND account=?2",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,channel,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,account,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE && sqlite3_changes(db->db)>0?0:-1;
}

int chanserv_db_access_get(ChanServDb *db, const char *channel, const char *account,
                           ChanServAccess *record) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||!channel_arg_fits(channel)||!account_arg_fits(account)||record==NULL)return -1;
    memset(record,0,sizeof(*record));
    if(sqlite3_prepare_v2(db->db,"SELECT channel,account,level FROM access WHERE channel=?1 AND account=?2",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,channel,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,account,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt);
    if(rc==SQLITE_ROW){
        if(copy_text_column(stmt,0,record->channel,sizeof(record->channel))!=0||
           copy_text_column(stmt,1,record->account,sizeof(record->account))!=0){
            sqlite3_finalize(stmt);memset(record,0,sizeof(*record));return -1;
        }
        record->level=(ChanServAccessLevel)sqlite3_column_int(stmt,2); sqlite3_finalize(stmt); return 1;
    }
    sqlite3_finalize(stmt); return rc==SQLITE_DONE?0:-1;
}

int chanserv_db_access_foreach(ChanServDb *db, const char *channel,
                               ChanServAccessCallback callback, void *context) {
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (db == NULL || db->db == NULL || !channel_arg_fits(channel) || callback == NULL) return -1;
    if (sqlite3_prepare_v2(db->db,
        "SELECT channel,account,level FROM access WHERE channel=?1 "
        "ORDER BY level DESC,account COLLATE NOCASE",
        -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, channel, -1, SQLITE_TRANSIENT);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ChanServAccess record;
        int callback_rc;
        memset(&record, 0, sizeof(record));
        if (copy_text_column(stmt, 0, record.channel, sizeof(record.channel)) != 0 ||
            copy_text_column(stmt, 1, record.account, sizeof(record.account)) != 0) {
            sqlite3_finalize(stmt);
            return -1;
        }
        record.level = (ChanServAccessLevel)sqlite3_column_int(stmt, 2);
        callback_rc = callback(&record, context);
        if (callback_rc != 0) {
            sqlite3_finalize(stmt);
            return callback_rc;
        }
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_db_access_list(ChanServDb *db, const char *channel, char *buffer, size_t size) {
    sqlite3_stmt *stmt=NULL; size_t used=0U; int rc; int truncated=0;
    if(db==NULL||db->db==NULL||!channel_arg_fits(channel)||buffer==NULL||size==0U)return -1;
    buffer[0]='\0';
    if(sqlite3_prepare_v2(db->db,"SELECT account,level FROM access WHERE channel=?1 ORDER BY level DESC,account COLLATE NOCASE",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,channel,-1,SQLITE_TRANSIENT);
    while((rc=sqlite3_step(stmt))==SQLITE_ROW){
        const unsigned char *raw=(const unsigned char *)sqlite3_column_text(stmt,0);
        int bytes=sqlite3_column_bytes(stmt,0);
        const char *account=(const char *)raw;
        int level=sqlite3_column_int(stmt,1);
        char item[96];
        int written;
        size_t n;
        if(raw==NULL||bytes<0||(size_t)bytes>IRC_NICK_MAX||
           (bytes!=0&&memchr(raw,'\0',(size_t)bytes)!=NULL)){truncated=1;break;}
        written=snprintf(item,sizeof(item),"%s%s:%d",used?" ":"",account,level);
        n=written>0?(size_t)written:0U;
        if(written<0||n>=sizeof(item)||n>=size-used){truncated=1;break;}
        memcpy(buffer+used,item,n); used+=n; buffer[used]='\0';
    }
    sqlite3_finalize(stmt);
    if (truncated) return -1;
    return rc==SQLITE_DONE?0:-1;
}
