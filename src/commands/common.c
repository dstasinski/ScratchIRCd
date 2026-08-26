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
#include <string.h>
#include <sys/socket.h>
#include <time.h>

const char *command_reply_nick(const Client *client){return(client==NULL||client->nick[0]=='\0')?"*":client->nick;}

static int registration_geo_banned(Server *server,Client *client){GeoBanDb db={0};GeoBanRecord record;int matched;if(geoban_db_open(&db,server->config.bans_db)!=0)return 0;matched=geoban_db_match(&db,&client->geoip,&record);if(matched==1){snotice_broadcast(server,SNOTICE_GEOBANS,"Registration rejected by GEOBAN: %s!%s@%s [real_ip=%s] matched %s {%s}",command_reply_nick(client),client->user,client->display_host,client->real_ip,geoban_type_name(record.type),record.value);client_sendf(client,ERR_YOUREBANNEDCREEP,server->config.server_name,command_reply_nick(client),server->config.admin_email);(void)snprintf(client->quit_reason,sizeof(client->quit_reason),"%s",record.reason[0]!='\0'?record.reason:"GeoIP policy ban");(void)shutdown(client->fd,SHUT_RDWR);}geoban_db_close(&db);return matched==1;}

static int registration_banned(Server *server,Client *client){BanDb db={0};BanRecord record;char host_identity[IRCD_MESSAGE_BUFFER_SIZE],ip_identity[IRCD_MESSAGE_BUFFER_SIZE];const char *real_host_identity=NULL;int matched=0;BanType matched_type=BAN_TYPE_ZLINE;if(ban_db_open(&db,server->config.bans_db)!=0)return 0;if(ban_db_match(&db,BAN_TYPE_ZLINE,client->real_ip,NULL,&record)==1){matched=1;matched_type=BAN_TYPE_ZLINE;}else{if(client->real_host[0]!='\0'){(void)snprintf(host_identity,sizeof(host_identity),"%s@%s",client->user,client->real_host);real_host_identity=host_identity;}(void)snprintf(ip_identity,sizeof(ip_identity),"%s@%s",client->user,client->real_ip);if(ban_db_match(&db,BAN_TYPE_KLINE,real_host_identity!=NULL?real_host_identity:ip_identity,ip_identity,&record)==1){matched=1;matched_type=BAN_TYPE_KLINE;}}if(matched){snotice_broadcast(server,SNOTICE_BANS,"Registration rejected by %s: %s!%s@%s [real_ip=%s] matched %s",matched_type==BAN_TYPE_ZLINE?"ZLINE":"KLINE",command_reply_nick(client),client->user,client->display_host,client->real_ip,record.mask);client_sendf(client,ERR_YOUREBANNEDCREEP,server->config.server_name,command_reply_nick(client),server->config.admin_email);(void)snprintf(client->quit_reason,sizeof(client->quit_reason),"%s",record.reason[0]!='\0'?record.reason:"Banned");(void)shutdown(client->fd,SHUT_RDWR);}ban_db_close(&db);return matched;}

static void load_pchannels(Server *server,char *buffer,size_t size){
    static int index_ensured=0;
    ChanServDb db={0};
    sqlite3_stmt *stmt=NULL;
    size_t used=0U;
    int rc;
    if(buffer==NULL||size==0U)return;
    buffer[0]='\0';
    if(server==NULL||chanserv_db_open(&db,server->config.chanserv_db)!=0)return;
    if(!index_ensured){
        char *error=NULL;
        if(sqlite3_exec(db.db,"CREATE INDEX IF NOT EXISTS channels_enabled_name_idx ON channels(enabled,name COLLATE IRCNOCASE)",NULL,NULL,&error)==SQLITE_OK)index_ensured=1;
        sqlite3_free(error);
    }
    if(sqlite3_prepare_v2(db.db,"SELECT name FROM channels WHERE enabled=1 ORDER BY name COLLATE IRCNOCASE LIMIT ?1",-1,&stmt,NULL)==SQLITE_OK){
        sqlite3_bind_int64(stmt,1,(sqlite3_int64)IRCD_ISUPPORT_PCHANNELS_MAX);
        while((rc=sqlite3_step(stmt))==SQLITE_ROW){
            const char *name=(const char *)sqlite3_column_text(stmt,0);
            size_t n=name!=NULL?strlen(name):0U;
            size_t need=n+(used?1U:0U);
            if(n==0U)continue;
            if(need>=size-used)break;
            if(used)buffer[used++]=',';
            memcpy(buffer+used,name,n);
            used+=n;
            buffer[used]='\0';
        }
        sqlite3_finalize(stmt);
    }
    chanserv_db_close(&db);
}

static void send_isupport(Server *server,Client *client){char first[IRCD_MESSAGE_BUFFER_SIZE],second[IRCD_MESSAGE_BUFFER_SIZE],pchannels[IRCD_MESSAGE_BUFFER_SIZE/2U];load_pchannels(server,pchannels,sizeof(pchannels));(void)snprintf(first,sizeof(first),"CASEMAPPING=rfc1459 CHANTYPES=#& PREFIX=(qaohv)~&@%%+ CHANMODES=beI,,kljBL,AciKMmnOprRSstTVz CHANLIMIT=#&:%u NICKLEN=%u USERLEN=%u HOSTLEN=%u CHANNELLEN=%u TOPICLEN=%u KICKLEN=%u MODES=%u NETWORK=%s",(unsigned)IRC_MAX_CHANNELS_PER_CLIENT,(unsigned)IRC_NICK_MAX,(unsigned)IRC_USER_MAX,(unsigned)IRC_HOST_MAX,(unsigned)IRC_CHANNEL_NAME_MAX,(unsigned)IRC_CHANNEL_TOPIC_MAX,(unsigned)IRC_KICK_REASON_MAX,(unsigned)IRC_MODE_MAX_PARAMS,server->config.network_name);(void)snprintf(second,sizeof(second),"EXCEPTS=e INVEX=I WATCH=%u SILENCE=%u TARGMAX=PRIVMSG:1,NOTICE:1,JOIN:1,PART:1,KICK:1,NAMES:1 MSGREFTYPES=timestamp CHATHISTORY=%zu PCHANNELS=%s",(unsigned)IRCD_WATCH_MAX,(unsigned)IRCD_SILENCE_MAX,server->config.history_limit,pchannels);client_sendf(client,RPL_PROTOCOLS,server->config.server_name,client->nick,first);client_sendf(client,RPL_PROTOCOLS,server->config.server_name,client->nick,second);}

void command_maybe_register(Server *server,Client *client){if(server==NULL||client==NULL||client->registered||client->nick[0]=='\0'||client->user[0]=='\0'||client->cap_negotiating||client->dns_state==CLIENT_DNS_PENDING||client->dns_state==CLIENT_DNS_NONE||(server->config.server_password[0]!='\0'&&!client->pass_accepted)||(server->config.nospoof_enabled&&(!client->nospoof_started||!client->nospoof_verified)))return;if(!client->geoip_complete){geoip_lookup(&server->geoip,client->real_ip,&client->geoip);client->geoip_complete=1;}if(registration_geo_banned(server,client))return;if(client->dnsbl_state==CLIENT_DNSBL_NONE){if(server->config.dnsbl_count==0U)client->dnsbl_state=CLIENT_DNSBL_CLEAR;else if(dnsbl_resolver_submit(&server->dnsbl,client->id,client->real_ip,server->config.dnsbls,server->config.dnsbl_count)==0){client->dnsbl_state=CLIENT_DNSBL_PENDING;client->dnsbl_deadline=time(NULL)+(time_t)server->config.dnsbl_timeout_seconds;return;}else{client->dnsbl_state=CLIENT_DNSBL_ERROR;snotice_broadcast(server,SNOTICE_DNS|SNOTICE_FLOOD,"DNSBL resolver queue saturated/unavailable for %s; registration fails open",client->real_ip);}}if(client->dnsbl_state==CLIENT_DNSBL_PENDING||client->dnsbl_state==CLIENT_DNSBL_LISTED)return;if(registration_banned(server,client))return;client->registered=1;client_sendf(client,RPL_WELCOME,server->config.server_name,client->nick,server->config.network_name,client->nick,client->user,client->display_host);client_sendf(client,RPL_YOURHOST,server->config.server_name,client->nick,server->config.server_name,IRCD_VERSION);client_sendf(client,RPL_CREATED,server->config.server_name,client->nick,IRCD_CREATED);client_sendf(client,RPL_AVAILABLE,server->config.server_name,client->nick,server->config.server_name,IRCD_VERSION,IRCD_SUPPORTED_USER_MODES,IRCD_SUPPORTED_CHANNEL_MODES);send_isupport(server,client);presence_watch_online(server,client);snotice_broadcast(server,SNOTICE_CONNECTIONS,"Client registered: %s!%s@%s [real_ip=%s real_host=%s%s]",client->nick,client->user,client->display_host,client->real_ip,client->real_host[0]!='\0'?client->real_host:"-",client->webirc.active?" webirc":"");}

int command_require_registered(Client *client){if(client!=NULL&&client->registered)return 0;if(client!=NULL)client_sendf(client,ERR_NOTREGISTERED,IRCD_DEFAULT_SERVER_NAME,command_reply_nick(client));return 1;}

void command_send_names(Channel *channel,Client *client){char names[IRC_NAMES_BUFFER_SIZE];size_t used=0U;ChannelMember *member;char marker;if(channel==NULL||client==NULL)return;marker=channel->name[0]=='&'?IRC_NAMES_PRIVATE_MARKER:IRC_NAMES_PUBLIC_MARKER;names[0]='\0';for(member=channel->members;member!=NULL;member=member->next){char prefix=channel_privilege_prefix(member->privileges);int written=prefix!='\0'?snprintf(names+used,sizeof(names)-used,"%s%c%s",used?" ":"",prefix,member->client->nick):snprintf(names+used,sizeof(names)-used,"%s%s",used?" ":"",member->client->nick);if(written<0||(size_t)written>=sizeof(names)-used)break;used+=(size_t)written;}client_sendf(client,RPL_NAMREPLY,IRCD_DEFAULT_SERVER_NAME,client->nick,marker,channel->name,names);client_sendf(client,RPL_ENDOFNAMES,IRCD_DEFAULT_SERVER_NAME,client->nick,channel->name);}
