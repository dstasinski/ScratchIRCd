#ifndef IRCD_CHANSERV_DB_H
#define IRCD_CHANSERV_DB_H
#include <sqlite3.h>
#include <stddef.h>
#include "config.h"
typedef struct ChanServChannel{char name[IRC_CHANNEL_NAME_MAX+1U];char founder[IRC_NICK_MAX+1U];char description[IRCD_CHANSERV_DESCRIPTION_MAX+1U];int enabled;long long created_at;long long updated_at;}ChanServChannel;
typedef struct ChanServDb{sqlite3*db;}ChanServDb;
int chanserv_db_open(ChanServDb*,const char*);void chanserv_db_close(ChanServDb*);int chanserv_db_get(ChanServDb*,const char*,ChanServChannel*);int chanserv_db_create(ChanServDb*,const char*,const char*,const char*);int chanserv_db_delete(ChanServDb*,const char*);int chanserv_db_set_enabled(ChanServDb*,const char*,int);int chanserv_db_set_founder(ChanServDb*,const char*,const char*);int chanserv_db_set_description(ChanServDb*,const char*,const char*);
/** Write comma-separated enabled persistent channel names into buffer. */
int chanserv_db_list_enabled(ChanServDb*,char *buffer,size_t buffer_size);
#endif
