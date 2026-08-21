/** @file chanserv_db.c @brief SQLite persistence for registered channels. */
#include "chanserv_db.h"
#include <stdio.h>
#include <string.h>

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

/**
 * Return non-zero when the existing access table accepts the new PROTECTED
 * value 5. ScratchIRCd 0.19 created the table with CHECK(level BETWEEN 1 AND 4),
 * so CREATE TABLE IF NOT EXISTS alone cannot upgrade an existing database.
 */
static int access_table_supports_protected(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int supported = 0;

    if (sqlite3_prepare_v2(db,
        "SELECT sql FROM sqlite_master WHERE type='table' AND name='access'",
        -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *sql = (const char *)sqlite3_column_text(stmt, 0);
        if (sql != NULL && strstr(sql, "BETWEEN 1 AND 5") != NULL)
            supported = 1;
    }
    sqlite3_finalize(stmt);
    return supported;
}

/**
 * Upgrade the 0.19 access table without renumbering existing rows.
 *
 * OWNER was stored as level 4 in 0.19 and remains level 4. PROTECTED is the
 * new level 5. Rebuilding the table only widens its CHECK constraint, so all
 * existing account access semantics remain unchanged.
 */
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
    if (sqlite3_create_collation(db->db, "IRCNOCASE", SQLITE_UTF8, NULL, irc_collation) != SQLITE_OK) {
        chanserv_db_close(db); return -1;
    }
    sqlite3_busy_timeout(db->db, 2000);
    if (exec_sql(db->db, "PRAGMA foreign_keys=ON;") != 0 ||
        exec_sql(db->db, schema) != 0 ||
        ensure_channel_columns(db->db) != 0 ||
        ensure_access_schema(db->db) != 0) {
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
    if (db == NULL || db->db == NULL || name == NULL || record == NULL) return -1;
    memset(record, 0, sizeof(*record));
    if (sqlite3_prepare_v2(db->db,
        "SELECT name,founder,description,enabled,mode_lock,topic,topic_setter,topic_time,created_at,updated_at "
        "FROM channels WHERE name=?1", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        (void)snprintf(record->name, sizeof(record->name), "%s", sqlite3_column_text(stmt, 0));
        (void)snprintf(record->founder, sizeof(record->founder), "%s", sqlite3_column_text(stmt, 1));
        (void)snprintf(record->description, sizeof(record->description), "%s", sqlite3_column_text(stmt, 2));
        record->enabled = sqlite3_column_int(stmt, 3);
        record->mode_lock = (uint64_t)sqlite3_column_int64(stmt, 4);
        (void)snprintf(record->topic, sizeof(record->topic), "%s", sqlite3_column_text(stmt, 5));
        (void)snprintf(record->topic_setter, sizeof(record->topic_setter), "%s", sqlite3_column_text(stmt, 6));
        record->topic_time = sqlite3_column_int64(stmt, 7);
        record->created_at = sqlite3_column_int64(stmt, 8);
        record->updated_at = sqlite3_column_int64(stmt, 9);
        sqlite3_finalize(stmt); return 1;
    }
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int chanserv_db_create(ChanServDb *db, const char *name, const char *founder, const char *description) {
    sqlite3_stmt *stmt = NULL; int rc;
    if (db == NULL || db->db == NULL || name == NULL || founder == NULL) return -1;
    if (sqlite3_prepare_v2(db->db,
        "INSERT INTO channels(name,founder,description) VALUES(?1,?2,?3)", -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt,1,name,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,2,founder,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,3,description != NULL ? description : "",-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE ? 0 : -1;
}

static int update_text(ChanServDb *db, const char *name, const char *column, const char *value) {
    char sql[192]; sqlite3_stmt *stmt=NULL; int rc;
    if (db==NULL || db->db==NULL || name==NULL || value==NULL) return -1;
    (void)snprintf(sql,sizeof(sql),"UPDATE channels SET %s=?1,updated_at=unixepoch() WHERE name=?2",column);
    if(sqlite3_prepare_v2(db->db,sql,-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,value,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,name,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE && sqlite3_changes(db->db)>0 ? 0 : -1;
}

int chanserv_db_set_description(ChanServDb *db,const char *name,const char *description){return update_text(db,name,"description",description);}
int chanserv_db_set_founder(ChanServDb *db,const char *name,const char *founder){return update_text(db,name,"founder",founder);}

int chanserv_db_set_enabled(ChanServDb *db, const char *name, int enabled) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||name==NULL)return -1;
    if(sqlite3_prepare_v2(db->db,"UPDATE channels SET enabled=?1,updated_at=unixepoch() WHERE name=?2",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_int(stmt,1,enabled?1:0); sqlite3_bind_text(stmt,2,name,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE && sqlite3_changes(db->db)>0 ? 0 : -1;
}

int chanserv_db_set_mode_lock(ChanServDb *db, const char *name, uint64_t mode_lock) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||name==NULL)return -1;
    if(sqlite3_prepare_v2(db->db,"UPDATE channels SET mode_lock=?1,updated_at=unixepoch() WHERE name=?2",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_int64(stmt,1,(sqlite3_int64)mode_lock); sqlite3_bind_text(stmt,2,name,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE && sqlite3_changes(db->db)>0 ? 0 : -1;
}

int chanserv_db_set_topic(ChanServDb *db, const char *name, const char *topic,
                          const char *setter, long long topic_time) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||name==NULL||topic==NULL||setter==NULL)return -1;
    if(sqlite3_prepare_v2(db->db,
        "UPDATE channels SET topic=?1,topic_setter=?2,topic_time=?3,updated_at=unixepoch() WHERE name=?4",
        -1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,topic,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,setter,-1,SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt,3,(sqlite3_int64)topic_time); sqlite3_bind_text(stmt,4,name,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE && sqlite3_changes(db->db)>0 ? 0 : -1;
}

int chanserv_db_delete(ChanServDb *db, const char *name) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||name==NULL)return -1;
    if(sqlite3_prepare_v2(db->db,"DELETE FROM channels WHERE name=?1",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,name,-1,SQLITE_TRANSIENT); rc=sqlite3_step(stmt); sqlite3_finalize(stmt);
    return rc==SQLITE_DONE && sqlite3_changes(db->db)>0 ? 0 : -1;
}

int chanserv_db_list_enabled(ChanServDb *db, char *buffer, size_t size) {
    sqlite3_stmt *stmt=NULL; size_t used=0U; int rc;
    if(db==NULL||db->db==NULL||buffer==NULL||size==0U)return -1;
    buffer[0]='\0';
    if(sqlite3_prepare_v2(db->db,"SELECT name FROM channels WHERE enabled=1 ORDER BY name COLLATE IRCNOCASE",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    while((rc=sqlite3_step(stmt))==SQLITE_ROW){
        const char *name=(const char *)sqlite3_column_text(stmt,0); size_t n=strlen(name),need=n+(used?1U:0U);
        if(need>=size-used)break; if(used)buffer[used++]=','; memcpy(buffer+used,name,n); used+=n; buffer[used]='\0';
    }
    sqlite3_finalize(stmt); return rc==SQLITE_DONE||rc==SQLITE_ROW?0:-1;
}

int chanserv_db_access_set(ChanServDb *db, const char *channel, const char *account,
                           ChanServAccessLevel level) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||channel==NULL||account==NULL||
       level<CHANSERV_ACCESS_VOICE||
       (level>CHANSERV_ACCESS_OWNER && level!=CHANSERV_ACCESS_PROTECTED)) return -1;
    if(sqlite3_prepare_v2(db->db,
        "INSERT INTO access(channel,account,level) VALUES(?1,?2,?3) "
        "ON CONFLICT(channel,account) DO UPDATE SET level=excluded.level,updated_at=unixepoch()",
        -1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,channel,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,account,-1,SQLITE_TRANSIENT); sqlite3_bind_int(stmt,3,(int)level);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE?0:-1;
}

int chanserv_db_access_delete(ChanServDb *db, const char *channel, const char *account) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||channel==NULL||account==NULL)return -1;
    if(sqlite3_prepare_v2(db->db,"DELETE FROM access WHERE channel=?1 AND account=?2",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,channel,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,account,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt); sqlite3_finalize(stmt); return rc==SQLITE_DONE && sqlite3_changes(db->db)>0?0:-1;
}

int chanserv_db_access_get(ChanServDb *db, const char *channel, const char *account,
                           ChanServAccess *record) {
    sqlite3_stmt *stmt=NULL; int rc;
    if(db==NULL||db->db==NULL||channel==NULL||account==NULL||record==NULL)return -1;
    memset(record,0,sizeof(*record));
    if(sqlite3_prepare_v2(db->db,"SELECT channel,account,level FROM access WHERE channel=?1 AND account=?2",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,channel,-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,2,account,-1,SQLITE_TRANSIENT);
    rc=sqlite3_step(stmt);
    if(rc==SQLITE_ROW){
        (void)snprintf(record->channel,sizeof(record->channel),"%s",sqlite3_column_text(stmt,0));
        (void)snprintf(record->account,sizeof(record->account),"%s",sqlite3_column_text(stmt,1));
        record->level=(ChanServAccessLevel)sqlite3_column_int(stmt,2); sqlite3_finalize(stmt); return 1;
    }
    sqlite3_finalize(stmt); return rc==SQLITE_DONE?0:-1;
}

int chanserv_db_access_list(ChanServDb *db, const char *channel, char *buffer, size_t size) {
    sqlite3_stmt *stmt=NULL; size_t used=0U; int rc;
    if(db==NULL||db->db==NULL||channel==NULL||buffer==NULL||size==0U)return -1;
    buffer[0]='\0';
    if(sqlite3_prepare_v2(db->db,"SELECT account,level FROM access WHERE channel=?1 ORDER BY level DESC,account COLLATE NOCASE",-1,&stmt,NULL)!=SQLITE_OK)return -1;
    sqlite3_bind_text(stmt,1,channel,-1,SQLITE_TRANSIENT);
    while((rc=sqlite3_step(stmt))==SQLITE_ROW){
        const char *account=(const char *)sqlite3_column_text(stmt,0); int level=sqlite3_column_int(stmt,1); char item[96];
        int written=snprintf(item,sizeof(item),"%s%s:%d",used?" ":"",account,level); size_t n=written>0?(size_t)written:0U;
        if(written<0||n>=sizeof(item)||n>=size-used)break; memcpy(buffer+used,item,n); used+=n; buffer[used]='\0';
    }
    sqlite3_finalize(stmt); return rc==SQLITE_DONE||rc==SQLITE_ROW?0:-1;
}
