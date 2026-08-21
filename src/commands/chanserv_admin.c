/** @file chanserv_admin.c @brief Network-admin management of registered channels. */
#include "commands.h"
#include "chanserv_db.h"
#include "chanserv.h"
#include "modes.h"
#include "nickserv_db.h"
#include "numerics.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int require_netadmin(Server *server, Client *client){if(!client_mode_has(client->modes,CLIENT_MODE_NETADMIN)){client_sendf(client,ERR_NOPRIVILEGES,server->config.server_name,client->nick);return 1;}return 0;}
static void notice(Server *server,Client *client,const char *text){client_sendf(client,":%s NOTICE %s :%s",server->config.server_name,client->nick,text);}

CommandResult command_csinfo(Server *server,Client *client,char *params){ChanServDb db={0};ChanServChannel r;char line[IRCD_OUTPUT_BUFFER_SIZE];char *name;
 if(command_require_registered(client)||require_netadmin(server,client))return COMMAND_KEEP_CLIENT; name=params?strtok(params," "):NULL;
 if(name==NULL){client_sendf(client,ERR_NEEDMOREPARAMS,server->config.server_name,client->nick,"CSINFO");return COMMAND_KEEP_CLIENT;}
 if(chanserv_db_open(&db,server->config.chanserv_db)!=0||chanserv_db_get(&db,name,&r)!=1){chanserv_db_close(&db);notice(server,client,"No such ChanServ channel.");return COMMAND_KEEP_CLIENT;}chanserv_db_close(&db);
 snprintf(line,sizeof(line),"CHANSERV %s founder=%s enabled=%d created=%lld updated=%lld description=%s",r.name,r.founder,r.enabled,r.created_at,r.updated_at,r.description[0]?r.description:"-");notice(server,client,line);return COMMAND_KEEP_CLIENT;}

CommandResult command_csset(Server *server,Client *client,char *params){ChanServDb db={0};char *name,*field,*value;int rc=-1;
 if(command_require_registered(client)||require_netadmin(server,client))return COMMAND_KEEP_CLIENT; if(params==NULL)goto bad;
 name=strtok(params," ");field=strtok(NULL," ");value=strtok(NULL,"");if(value)while(*value==' ')++value;if(!name||!field||!value||!*value)goto bad;
 if(chanserv_db_open(&db,server->config.chanserv_db)!=0){notice(server,client,"CSSET failed.");return COMMAND_KEEP_CLIENT;}
 if(strcasecmp(field,"ENABLED")==0&&(strcmp(value,"0")==0||strcmp(value,"1")==0))rc=chanserv_db_set_enabled(&db,name,value[0]=='1');
 else if(strcasecmp(field,"DESCRIPTION")==0&&strlen(value)<=IRCD_CHANSERV_DESCRIPTION_MAX)rc=chanserv_db_set_description(&db,name,value);
 else if(strcasecmp(field,"FOUNDER")==0){NickServDb ndb={0};NickServAccount a;if(nickserv_db_open(&ndb,server->config.nickserv_db)==0&&nickserv_db_get(&ndb,value,&a)==1&&a.enabled)rc=chanserv_db_set_founder(&db,name,a.name);nickserv_db_close(&ndb);}
 chanserv_db_close(&db);
 if(rc==0){Channel *ch=hash_get(&server->channels_by_name,name);if(ch){if(strcasecmp(field,"ENABLED")==0&&value[0]=='0')ch->modes=channel_mode_remove(ch->modes,CHANNEL_MODE_REGISTERED);else chanserv_apply_registration(server,ch);server_remove_channel_if_empty(server,ch);}notice(server,client,"ChanServ channel updated.");}else notice(server,client,"CSSET failed.");return COMMAND_KEEP_CLIENT;
bad: client_sendf(client,ERR_NEEDMOREPARAMS,server->config.server_name,client->nick,"CSSET");return COMMAND_KEEP_CLIENT;}

CommandResult command_csdrop(Server *server,Client *client,char *params){ChanServDb db={0};char *name;Channel *ch;
 if(command_require_registered(client)||require_netadmin(server,client))return COMMAND_KEEP_CLIENT;name=params?strtok(params," "):NULL;if(!name){client_sendf(client,ERR_NEEDMOREPARAMS,server->config.server_name,client->nick,"CSDROP");return COMMAND_KEEP_CLIENT;}
 if(chanserv_db_open(&db,server->config.chanserv_db)!=0||chanserv_db_delete(&db,name)!=0){chanserv_db_close(&db);notice(server,client,"CSDROP failed.");return COMMAND_KEEP_CLIENT;}chanserv_db_close(&db);
 ch=hash_get(&server->channels_by_name,name);if(ch){ch->modes=channel_mode_remove(ch->modes,CHANNEL_MODE_REGISTERED);server_remove_channel_if_empty(server,ch);}notice(server,client,"ChanServ channel deleted.");return COMMAND_KEEP_CLIENT;}
