/** @file common.c @brief Shared command helpers and registration. */
#include "commands.h"
#include "ban_db.h"
#include "chanserv_db.h"
#include "config.h"
#include "geoban_db.h"
#include "geoip.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"
#include "presence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

const char *command_reply_nick(const Client *client){return(client==NULL||client->nick[0]=='\0')?"*":client->nick;}

/* Pre-registration commands do not all carry Server through their shared
 * registration check. Keep the active runtime name here so 451 replies use
 * the configured server identity rather than the compile-time default. */
static char command_server_name[IRC_HOST_MAX+1U]=IRCD_DEFAULT_SERVER_NAME;

void command_common_set_server_name(const char *server_name){
    if(server_name==NULL||*server_name=='\0')
        (void)snprintf(command_server_name,sizeof(command_server_name),"%s",IRCD_DEFAULT_SERVER_NAME);
    else
        (void)snprintf(command_server_name,sizeof(command_server_name),"%s",server_name);
}

/* Registration policy is consulted for every client but changes relatively
 * rarely. Keep persistent SQLite handles rather than reopening bans.db twice
 * per registration. Policy itself remains database-backed, so KLINE/ZLINE and
 * GEOBAN changes become visible immediately on these same live connections. */
static BanDb registration_ban_db={0};
static GeoBanDb registration_geoban_db={0};
static char registration_ban_path[IRCD_CONFIG_PATH_MAX+1U];
static char registration_geoban_path[IRCD_CONFIG_PATH_MAX+1U];

static BanDb *registration_ban_handle(Server *server){
    if(server==NULL||server->config.bans_db[0]=='\0')return NULL;
    if(registration_ban_db.handle!=NULL&&strcmp(registration_ban_path,server->config.bans_db)==0)return &registration_ban_db;
    if(registration_ban_db.handle!=NULL){ban_db_close(&registration_ban_db);registration_ban_path[0]='\0';}
    if(ban_db_open(&registration_ban_db,server->config.bans_db)!=0)return NULL;
    (void)snprintf(registration_ban_path,sizeof(registration_ban_path),"%s",server->config.bans_db);
    return &registration_ban_db;
}

static GeoBanDb *registration_geoban_handle(Server *server){
    if(server==NULL||server->config.bans_db[0]=='\0')return NULL;
    if(registration_geoban_db.handle!=NULL&&strcmp(registration_geoban_path,server->config.bans_db)==0)return &registration_geoban_db;
    if(registration_geoban_db.handle!=NULL){geoban_db_close(&registration_geoban_db);registration_geoban_path[0]='\0';}
    if(geoban_db_open(&registration_geoban_db,server->config.bans_db)!=0)return NULL;
    (void)snprintf(registration_geoban_path,sizeof(registration_geoban_path),"%s",server->config.bans_db);
    return &registration_geoban_db;
}

static int registration_geo_banned(Server *server,Client *client){GeoBanDb *db;GeoBanRecord record;int matched;db=registration_geoban_handle(server);if(db==NULL)return 0;matched=geoban_db_match(db,&client->geoip,&record);if(matched==1){snotice_broadcast(server,SNOTICE_GEOBANS,"Registration rejected by GEOBAN: %s!%s@%s [real_ip=%s] matched %s {%s}",command_reply_nick(client),client->user,client->display_host,client->real_ip,geoban_type_name(record.type),record.value);client_sendf(client,ERR_YOUREBANNEDCREEP,server->config.server_name,command_reply_nick(client),server->config.admin_email);(void)snprintf(client->quit_reason,sizeof(client->quit_reason),"%s",record.reason[0]!='\0'?record.reason:"GeoIP policy ban");(void)shutdown(client->fd,SHUT_RDWR);}return matched==1;}

static int registration_banned(Server *server,Client *client){BanDb *db;BanRecord record;char host_identity[IRCD_MESSAGE_BUFFER_SIZE],ip_identity[IRCD_MESSAGE_BUFFER_SIZE];const char *real_host_identity=NULL;int matched=0;BanType matched_type=BAN_TYPE_ZLINE;db=registration_ban_handle(server);if(db==NULL)return 0;if(ban_db_match(db,BAN_TYPE_ZLINE,client->real_ip,NULL,&record)==1){matched=1;matched_type=BAN_TYPE_ZLINE;}else{if(client->real_host[0]!='\0'){(void)snprintf(host_identity,sizeof(host_identity),"%s@%s",client->user,client->real_host);real_host_identity=host_identity;}(void)snprintf(ip_identity,sizeof(ip_identity),"%s@%s",client->user,client->real_ip);if(ban_db_match(db,BAN_TYPE_KLINE,real_host_identity!=NULL?real_host_identity:ip_identity,ip_identity,&record)==1){matched=1;matched_type=BAN_TYPE_KLINE;}}if(matched){snotice_broadcast(server,SNOTICE_BANS,"Registration rejected by %s: %s!%s@%s [real_ip=%s] matched %s",matched_type==BAN_TYPE_ZLINE?"ZLINE":"KLINE",command_reply_nick(client),client->user,client->display_host,client->real_ip,record.mask);client_sendf(client,ERR_YOUREBANNEDCREEP,server->config.server_name,command_reply_nick(client),server->config.admin_email);(void)snprintf(client->quit_reason,sizeof(client->quit_reason),"%s",record.reason[0]!='\0'?record.reason:"Banned");(void)shutdown(client->fd,SHUT_RDWR);}return matched;}

static int isupport_payload_fits(const Server *server,const Client *client,const char *payload){
    int written;
    if(server==NULL||client==NULL||payload==NULL)return 0;
    written=snprintf(NULL,0,RPL_PROTOCOLS,server->config.server_name,client->nick,payload);
    return written>=0&&(size_t)written<=IRC_LINE_CONTENT_MAX;
}

static void send_isupport_payload(Server *server,Client *client,const char *payload){
    if(isupport_payload_fits(server,client,payload))
        client_sendf(client,RPL_PROTOCOLS,server->config.server_name,client->nick,payload);
}

/* PCHANNELS changes rarely compared with client registrations. Keep one
 * process-local snapshot and rebuild it only after the ChanServ DB generation
 * changes (create/drop/enable/disable) or the configured DB path changes. */
static char *pchannels_cache=NULL;
static uint64_t pchannels_cache_generation=0U;
static char pchannels_cache_path[IRCD_CONFIG_PATH_MAX+1U];

void command_common_reset_state(void){
    if(registration_ban_db.handle!=NULL)ban_db_close(&registration_ban_db);
    if(registration_geoban_db.handle!=NULL)geoban_db_close(&registration_geoban_db);
    registration_ban_path[0]='\0';
    registration_geoban_path[0]='\0';
    free(pchannels_cache);
    pchannels_cache=NULL;
    pchannels_cache_generation=0U;
    pchannels_cache_path[0]='\0';
    command_common_set_server_name(IRCD_DEFAULT_SERVER_NAME);
}

static int rebuild_pchannels_cache(Server *server){
    ChanServDb db={0};
    sqlite3_stmt *stmt=NULL;
    char *fresh=NULL;
    size_t used=0U,capacity=1U;
    int rc=SQLITE_DONE;
    uint64_t generation;

    if(server==NULL)return -1;
    generation=chanserv_db_pchannels_generation();
    fresh=malloc(capacity);
    if(fresh==NULL)return -1;
    fresh[0]='\0';

    if(chanserv_db_open(&db,server->config.chanserv_db)!=0){free(fresh);return -1;}
    if(sqlite3_prepare_v2(db.db,"SELECT name FROM channels WHERE enabled=1 ORDER BY name COLLATE IRCNOCASE",-1,&stmt,NULL)!=SQLITE_OK){chanserv_db_close(&db);free(fresh);return -1;}

    while((rc=sqlite3_step(stmt))==SQLITE_ROW){
        const char *name=(const char *)sqlite3_column_text(stmt,0);
        size_t n=name!=NULL?strlen(name):0U;
        size_t needed;
        char *grown;
        if(n==0U)continue;
        needed=used+(used?1U:0U)+n+1U;
        if(needed>capacity){
            size_t next=capacity;
            while(next<needed){
                size_t doubled=next<4096U?4096U:next*2U;
                if(doubled<next){sqlite3_finalize(stmt);chanserv_db_close(&db);free(fresh);return -1;}
                next=doubled;
            }
            grown=realloc(fresh,next);
            if(grown==NULL){sqlite3_finalize(stmt);chanserv_db_close(&db);free(fresh);return -1;}
            fresh=grown;capacity=next;
        }
        if(used)fresh[used++]=',';
        memcpy(fresh+used,name,n);used+=n;fresh[used]='\0';
    }
    sqlite3_finalize(stmt);
    chanserv_db_close(&db);
    if(rc!=SQLITE_DONE){free(fresh);return -1;}

    free(pchannels_cache);
    pchannels_cache=fresh;
    pchannels_cache_generation=generation;
    (void)snprintf(pchannels_cache_path,sizeof(pchannels_cache_path),"%s",server->config.chanserv_db);
    return 0;
}

static const char *get_pchannels_cache(Server *server){
    uint64_t generation;
    if(server==NULL)return "";
    generation=chanserv_db_pchannels_generation();
    if(pchannels_cache==NULL||pchannels_cache_generation!=generation||strcmp(pchannels_cache_path,server->config.chanserv_db)!=0){
        if(rebuild_pchannels_cache(server)!=0)return pchannels_cache!=NULL?pchannels_cache:"";
    }
    return pchannels_cache!=NULL?pchannels_cache:"";
}

static void send_pchannels_isupport(Server *server,Client *client,const char *base){
    const char *cached;
    const char *cursor;
    char payload[IRC_LINE_CONTENT_MAX+1U];
    int names_in_chunk=0;

    if(server==NULL||client==NULL||base==NULL)return;
    cached=get_pchannels_cache(server);
    if(snprintf(payload,sizeof(payload),"%s PCHANNELS=",base)<0)return;
    cursor=cached;

    while(*cursor!='\0'){
        const char *comma=strchr(cursor,',');
        size_t n=comma!=(const char *)NULL?(size_t)(comma-cursor):strlen(cursor);
        char name[IRC_CHANNEL_NAME_MAX+1U];
        char candidate[IRC_LINE_CONTENT_MAX+1U];
        int written;
        if(n>IRC_CHANNEL_NAME_MAX)n=IRC_CHANNEL_NAME_MAX;
        memcpy(name,cursor,n);name[n]='\0';
        written=snprintf(candidate,sizeof(candidate),"%s%s%s",payload,names_in_chunk?",":"",name);
        if(written>=0&&(size_t)written<sizeof(candidate)&&isupport_payload_fits(server,client,candidate)){
            memcpy(payload,candidate,(size_t)written+1U);
            names_in_chunk=1;
        }else{
            send_isupport_payload(server,client,payload);
            written=snprintf(payload,sizeof(payload),"PCHANNELS=%s",name);
            if(written<0||(size_t)written>=sizeof(payload)||!isupport_payload_fits(server,client,payload))return;
            names_in_chunk=1;
        }
        cursor=comma!=NULL?comma+1U:cursor+strlen(cursor);
    }
    send_isupport_payload(server,client,payload);
}

static void send_isupport(Server *server,Client *client){
    char first[IRCD_MESSAGE_BUFFER_SIZE];
    char second_base[IRCD_MESSAGE_BUFFER_SIZE];
    (void)snprintf(first,sizeof(first),"CASEMAPPING=rfc1459 CHANTYPES=#& PREFIX=(qaohv)~&@%%+ CHANMODES=beI,,kljBL,AciKMmnOprRSstTVz CHANLIMIT=#&:%u NICKLEN=%u USERLEN=%u HOSTLEN=%u CHANNELLEN=%u TOPICLEN=%u KICKLEN=%u MODES=%u NETWORK=%s",(unsigned)IRC_MAX_CHANNELS_PER_CLIENT,(unsigned)IRC_NICK_MAX,(unsigned)IRC_USER_MAX,(unsigned)IRC_HOST_MAX,(unsigned)IRC_CHANNEL_NAME_MAX,(unsigned)IRC_CHANNEL_TOPIC_MAX,(unsigned)IRC_KICK_REASON_MAX,(unsigned)IRC_MODE_MAX_PARAMS,server->config.network_name);
    (void)snprintf(second_base,sizeof(second_base),"EXCEPTS=e INVEX=I MAXLIST=b:%u,e:%u,I:%u WATCH=%u SILENCE=%u TARGMAX=PRIVMSG:1,NOTICE:1,JOIN:1,PART:1,KICK:1,NAMES:1 MSGREFTYPES=timestamp CHATHISTORY=%zu",(unsigned)IRC_CHANNEL_MASK_LIST_MAX,(unsigned)IRC_CHANNEL_MASK_LIST_MAX,(unsigned)IRC_CHANNEL_MASK_LIST_MAX,(unsigned)IRCD_WATCH_MAX,(unsigned)IRCD_SILENCE_MAX,server->config.history_limit);
    send_isupport_payload(server,client,first);
    send_pchannels_isupport(server,client,second_base);
}

void command_maybe_register(Server *server,Client *client){if(server==NULL||client==NULL||client->registered||client->nick[0]=='\0'||client->user[0]=='\0'||client->cap_negotiating||client->dns_state==CLIENT_DNS_PENDING||client->dns_state==CLIENT_DNS_NONE||(server->config.server_password[0]!='\0'&&!client->pass_accepted)||(server->config.nospoof_enabled&&(!client->nospoof_started||!client->nospoof_verified)))return;if(!client->geoip_complete){geoip_lookup(&server->geoip,client->real_ip,&client->geoip);client->geoip_complete=1;}if(registration_geo_banned(server,client))return;if(client->dnsbl_state==CLIENT_DNSBL_NONE){if(server->config.dnsbl_count==0U)client->dnsbl_state=CLIENT_DNSBL_CLEAR;else if(dnsbl_resolver_submit(&server->dnsbl,client->id,client->real_ip,server->config.dnsbls,server->config.dnsbl_count)==0){client->dnsbl_state=CLIENT_DNSBL_PENDING;client->dnsbl_deadline=time(NULL)+(time_t)server->config.dnsbl_timeout_seconds;return;}else{client->dnsbl_state=CLIENT_DNSBL_ERROR;snotice_broadcast(server,SNOTICE_DNS|SNOTICE_FLOOD,"DNSBL resolver queue saturated/unavailable for %s; registration fails open",client->real_ip);}}if(client->dnsbl_state==CLIENT_DNSBL_PENDING||client->dnsbl_state==CLIENT_DNSBL_LISTED)return;if(registration_banned(server,client))return;client->registered=1;client_sendf(client,RPL_WELCOME,server->config.server_name,client->nick,server->config.network_name,client->nick,client->user,client->display_host);client_sendf(client,RPL_YOURHOST,server->config.server_name,client->nick,server->config.server_name,IRCD_VERSION);client_sendf(client,RPL_CREATED,server->config.server_name,client->nick,IRCD_CREATED);client_sendf(client,RPL_AVAILABLE,server->config.server_name,client->nick,server->config.server_name,IRCD_VERSION,IRCD_SUPPORTED_USER_MODES,IRCD_SUPPORTED_CHANNEL_MODES);send_isupport(server,client);presence_watch_online(server,client);snotice_broadcast(server,SNOTICE_CONNECTIONS,"Client registered: %s!%s@%s [real_ip=%s real_host=%s%s]",client->nick,client->user,client->display_host,client->real_ip,client->real_host[0]!='\0'?client->real_host:"-",client->webirc.active?" webirc":"");}

int command_require_registered(Client *client){if(client!=NULL&&client->registered)return 0;if(client!=NULL)client_sendf(client,ERR_NOTREGISTERED,command_server_name,command_reply_nick(client));return 1;}

static int names_payload_fits(const Server *server,const Client *client,char marker,const Channel *channel,const char *names){
    int written;
    if(server==NULL||client==NULL||channel==NULL||names==NULL)return 0;
    written=snprintf(NULL,0,RPL_NAMREPLY,server->config.server_name,client->nick,marker,channel->name,names);
    return written>=0&&(size_t)written<=IRC_LINE_CONTENT_MAX;
}

static void send_names_chunk(const Server *server,Client *client,char marker,const Channel *channel,const char *names){
    if(server==NULL||client==NULL||channel==NULL||names==NULL)return;
    client_sendf(client,RPL_NAMREPLY,server->config.server_name,client->nick,marker,channel->name,names);
}

void command_send_names(Server *server,Channel *channel,Client *client){
    char names[IRC_LINE_CONTENT_MAX+1U];
    char candidate[IRC_LINE_CONTENT_MAX+1U];
    ChannelMember *member;
    char marker;
    size_t used=0U;
    if(server==NULL||channel==NULL||client==NULL)return;
    marker=channel->name[0]=='&'?IRC_NAMES_PRIVATE_MARKER:IRC_NAMES_PUBLIC_MARKER;
    names[0]='\0';
    for(member=channel->members;member!=NULL;member=member->next){
        char token[IRC_NICK_MAX+2U];
        char prefix=channel_privilege_prefix(member->privileges);
        int token_written=prefix!='\0'?snprintf(token,sizeof(token),"%c%s",prefix,member->client->nick):snprintf(token,sizeof(token),"%s",member->client->nick);
        int candidate_written;
        if(token_written<0||(size_t)token_written>=sizeof(token))continue;
        candidate_written=snprintf(candidate,sizeof(candidate),"%s%s%s",names,used?" ":"",token);
        if(candidate_written>=0&&(size_t)candidate_written<sizeof(candidate)&&names_payload_fits(server,client,marker,channel,candidate)){
            memcpy(names,candidate,(size_t)candidate_written+1U);
            used=(size_t)candidate_written;
            continue;
        }
        if(used!=0U)send_names_chunk(server,client,marker,channel,names);
        (void)snprintf(names,sizeof(names),"%s",token);
        used=strlen(names);
    }
    if(used!=0U||channel->member_count==0U)send_names_chunk(server,client,marker,channel,names);
    client_sendf(client,RPL_ENDOFNAMES,server->config.server_name,client->nick,channel->name);
}
