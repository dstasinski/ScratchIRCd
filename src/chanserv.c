/**
 * @file chanserv.c
 * @brief Virtual ChanServ service and registered-channel policy.
 *
 * ChanServ is server authority, not a Client: it never joins channels and is
 * never inserted into client/channel membership lists. Registered channels
 * remain as empty live Channel objects and carry service-controlled +r.
 */
#include "chanserv.h"
#include "chanserv_db.h"
#include "modes.h"
#include "nickserv_db.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static void notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":ChanServ!service@%s NOTICE %s :%s",
                 server->config.server_name, client->nick, text);
}

static int valid_channel_name(const char *name) {
    return name != NULL && strchr(IRC_CHANNEL_PREFIXES, name[0]) != NULL &&
           strlen(name) <= IRC_CHANNEL_NAME_MAX && strchr(name, ' ') == NULL;
}

static int account_exists(Server *server, const char *account_name) {
    NickServDb db = {0}; NickServAccount account; int rc;
    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) return 0;
    rc = nickserv_db_get(&db, account_name, &account);
    nickserv_db_close(&db);
    return rc == 1 && account.enabled;
}

int chanserv_channel_registered(Server *server, const char *channel_name) {
    ChanServDb db = {0}; ChanServChannel record; int rc;
    if (server == NULL || channel_name == NULL || chanserv_db_open(&db, server->config.chanserv_db) != 0) return 0;
    rc = chanserv_db_get(&db, channel_name, &record); chanserv_db_close(&db);
    return rc == 1 && record.enabled;
}

void chanserv_apply_registration(Server *server, Channel *channel) {
    ChanServDb db = {0}; ChanServChannel record;
    if (server == NULL || channel == NULL) return;
    if (chanserv_db_open(&db, server->config.chanserv_db) == 0) {
        if (chanserv_db_get(&db, channel->name, &record) == 1 && record.enabled)
            channel->modes = channel_mode_add(channel->modes, CHANNEL_MODE_REGISTERED);
        chanserv_db_close(&db);
    }
}

static void help(Server *server, Client *client) {
    notice(server, client, "Commands: REGISTER <#channel> [:description], INFO <#channel>, DROP <#channel>, HELP");
}

static void register_channel(Server *server, Client *client, char *rest) {
    ChanServDb db = {0}; Channel *channel; ChannelMember *member;
    char *name = strtok(rest, " "); char *description = strtok(NULL, "");
    if (description != NULL) { while (*description == ' ') ++description; if (*description == ':') ++description; }
    if (client->account_name[0] == '\0') { notice(server,client,"You must be identified to NickServ to register a channel."); return; }
    if (!valid_channel_name(name)) { notice(server,client,"Invalid channel name."); return; }
    channel = hash_get(&server->channels_by_name,name);
    if (channel == NULL || (member=channel_find_member(channel,client)) == NULL ||
        !channel_privilege_has(member->privileges, CHANNEL_PRIV_OWNER|CHANNEL_PRIV_OPERATOR)) {
        notice(server,client,"You must be an owner or operator in that channel to register it."); return;
    }
    if (description != NULL && strlen(description) > IRCD_CHANSERV_DESCRIPTION_MAX) { notice(server,client,"Channel description is too long."); return; }
    if (chanserv_db_open(&db,server->config.chanserv_db)!=0) { notice(server,client,"ChanServ database unavailable."); return; }
    if (chanserv_db_create(&db,name,client->account_name,description != NULL ? description : "") != 0) {
        chanserv_db_close(&db); notice(server,client,"That channel is already registered or registration failed."); return;
    }
    chanserv_db_close(&db);
    channel->modes = channel_mode_add(channel->modes, CHANNEL_MODE_REGISTERED);
    notice(server,client,"Channel registered. You are the founder.");
}

static void info_channel(Server *server, Client *client, char *rest) {
    ChanServDb db={0}; ChanServChannel record; char line[IRCD_OUTPUT_BUFFER_SIZE]; char *name=strtok(rest," ");
    if (!valid_channel_name(name) || chanserv_db_open(&db,server->config.chanserv_db)!=0 || chanserv_db_get(&db,name,&record)!=1) {
        chanserv_db_close(&db); notice(server,client,"Channel is not registered."); return;
    }
    chanserv_db_close(&db);
    snprintf(line,sizeof(line),"%s founder=%s enabled=%d created=%lld description=%s",
             record.name,record.founder,record.enabled,record.created_at,
             record.description[0] != '\0' ? record.description : "-");
    notice(server,client,line);
}

static void drop_channel(Server *server, Client *client, char *rest) {
    ChanServDb db={0}; ChanServChannel record; Channel *channel; char *name=strtok(rest," ");
    if (client->account_name[0]=='\0') { notice(server,client,"You must be identified to NickServ."); return; }
    if (!valid_channel_name(name) || chanserv_db_open(&db,server->config.chanserv_db)!=0 || chanserv_db_get(&db,name,&record)!=1) {
        chanserv_db_close(&db); notice(server,client,"Channel is not registered."); return;
    }
    if (strcasecmp(record.founder,client->account_name)!=0) { chanserv_db_close(&db); notice(server,client,"Only the channel founder may drop it."); return; }
    if (chanserv_db_delete(&db,name)!=0) { chanserv_db_close(&db); notice(server,client,"DROP failed."); return; }
    chanserv_db_close(&db);
    channel=hash_get(&server->channels_by_name,name);
    if(channel!=NULL){channel->modes=channel_mode_remove(channel->modes,CHANNEL_MODE_REGISTERED); server_remove_channel_if_empty(server,channel);}
    notice(server,client,"Channel registration dropped.");
}

void chanserv_handle_message(Server *server, Client *client, char *message) {
    char *command; char *rest;
    if(server==NULL||client==NULL||message==NULL)return;
    command=strtok(message," "); rest=strtok(NULL,""); if(rest!=NULL)while(*rest==' ')++rest;
    if(command==NULL)return;
    if(strcasecmp(command,"HELP")==0) help(server,client);
    else if(strcasecmp(command,"REGISTER")==0 && rest!=NULL) register_channel(server,client,rest);
    else if(strcasecmp(command,"INFO")==0 && rest!=NULL) info_channel(server,client,rest);
    else if(strcasecmp(command,"DROP")==0 && rest!=NULL) drop_channel(server,client,rest);
    else notice(server,client,"Unknown or incomplete ChanServ command. Use CHANSERV HELP.");
}
