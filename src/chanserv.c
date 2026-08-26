/**
 * @file chanserv.c
 * @brief Virtual ChanServ service for persistent channel registration and policy.
 *
 * ChanServ never exists as a Client and never joins channels. Registration and
 * access authority are bound to authenticated NickServ account names.
 */
#include "chanserv.h"
#include "chanserv_db.h"
#include "chanserv_persist.h"
#include "modes.h"
#include "nickserv_db.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void cs_notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":ChanServ!service@%s NOTICE %s :%s",
                 server->config.server_name, client->nick, text);
}

static int valid_channel_name(const char *name) {
    return name != NULL && strchr(IRC_CHANNEL_PREFIXES, name[0]) != NULL &&
           strlen(name) <= IRC_CHANNEL_NAME_MAX && strchr(name, ' ') == NULL;
}

static int founder_matches(const ChanServChannel *record, const Client *client) {
    return record != NULL && client != NULL && client->account_name[0] != '\0' &&
           strcasecmp(record->founder, client->account_name) == 0;
}

static int cached_founder_matches(const Channel *channel, const Client *client) {
    return channel != NULL && channel->chanserv_policy_valid && client != NULL &&
           client->account_name[0] != '\0' &&
           strcasecmp(channel->chanserv_founder, client->account_name) == 0;
}

static void clear_cached_policy(Channel *channel) {
    if (channel == NULL) return;
    channel->chanserv_policy_loaded = 1;
    channel->chanserv_policy_valid = 0;
    channel->chanserv_founder[0] = '\0';
    channel->chanserv_mode_lock = 0U;
    channel->modes = channel_mode_remove(channel->modes, CHANNEL_MODE_REGISTERED);
}

static void cache_policy(Channel *channel, const ChanServChannel *record) {
    if (channel == NULL || record == NULL || !record->enabled) {
        clear_cached_policy(channel);
        return;
    }
    channel->chanserv_policy_loaded = 1;
    channel->chanserv_policy_valid = 1;
    (void)snprintf(channel->chanserv_founder, sizeof(channel->chanserv_founder),
                   "%s", record->founder);
    channel->chanserv_mode_lock = (ChannelModeSet)record->mode_lock;
}

static ChannelModeSet persistent_mode_bit(char letter) {
    switch (letter) {
        case 'A': return CHANNEL_MODE_ADMIN_ONLY;
        case 'c': return CHANNEL_MODE_NO_COLOR;
        case 'i': return CHANNEL_MODE_INVITE_ONLY;
        case 'K': return CHANNEL_MODE_NO_KNOCK;
        case 'M': return CHANNEL_MODE_REGONLY_SPEAK;
        case 'm': return CHANNEL_MODE_MODERATED;
        case 'n': return CHANNEL_MODE_NO_EXTERNAL;
        case 'O': return CHANNEL_MODE_OPER_ONLY;
        case 'p': return CHANNEL_MODE_PRIVATE;
        case 'R': return CHANNEL_MODE_REGONLY_JOIN;
        case 'S': return CHANNEL_MODE_STRIP_COLOR;
        case 's': return CHANNEL_MODE_SECRET;
        case 't': return CHANNEL_MODE_TOPIC_LOCK;
        case 'T': return CHANNEL_MODE_NO_NOTICE;
        case 'V': return CHANNEL_MODE_NO_INVITE;
        case 'z': return CHANNEL_MODE_SECURE_ONLY;
        default: return 0U;
    }
}

static int parse_mode_lock(const char *text, ChannelModeSet *modes) {
    ChannelModeSet result = 0U;
    char sign = '+';
    size_t i;
    if (text == NULL || modes == NULL) return -1;
    for (i = 0U; text[i] != '\0'; ++i) {
        ChannelModeSet bit;
        if (text[i] == '+' || text[i] == '-') { sign = text[i]; continue; }
        bit = persistent_mode_bit(text[i]);
        if (bit == 0U) return -1;
        if (sign == '+') result |= bit;
        else result &= ~bit;
    }
    *modes = result;
    return 0;
}

static const char *access_name(ChanServAccessLevel level) {
    switch (level) {
        case CHANSERV_ACCESS_OWNER: return "OWNER";
        case CHANSERV_ACCESS_PROTECTED: return "PROTECTED";
        case CHANSERV_ACCESS_OP: return "OP";
        case CHANSERV_ACCESS_HALFOP: return "HALFOP";
        case CHANSERV_ACCESS_VOICE: return "VOICE";
        default: return "NONE";
    }
}

static ChanServAccessLevel parse_access(const char *text) {
    if (text == NULL) return CHANSERV_ACCESS_NONE;
    if (strcasecmp(text, "OWNER") == 0) return CHANSERV_ACCESS_OWNER;
    if (strcasecmp(text, "PROTECTED") == 0) return CHANSERV_ACCESS_PROTECTED;
    if (strcasecmp(text, "OP") == 0) return CHANSERV_ACCESS_OP;
    if (strcasecmp(text, "HALFOP") == 0) return CHANSERV_ACCESS_HALFOP;
    if (strcasecmp(text, "VOICE") == 0) return CHANSERV_ACCESS_VOICE;
    return CHANSERV_ACCESS_NONE;
}

static void load_channel_policy(Server *server, Channel *channel, int force) {
    ChanServDb db = {0};
    ChanServChannel record;
    int found;
    if (server == NULL || channel == NULL) return;
    if (!force && channel->chanserv_policy_loaded) return;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return;
    found = chanserv_db_get(&db, channel->name, &record);
    if (found == 1 && record.enabled) {
        cache_policy(channel, &record);
        channel->modes = (ChannelModeSet)record.mode_lock;
        channel->modes = channel_mode_add(channel->modes, CHANNEL_MODE_REGISTERED);
        (void)snprintf(channel->topic, sizeof(channel->topic), "%s", record.topic);
        (void)snprintf(channel->topic_setter, sizeof(channel->topic_setter), "%s", record.topic_setter);
        channel->topic_time = (time_t)record.topic_time;
        (void)chanserv_persist_restore(server->config.chanserv_db, channel);
    } else if (found == 0 || (found == 1 && !record.enabled)) {
        clear_cached_policy(channel);
    }
    chanserv_db_close(&db);
}

void chanserv_restore_channel(Server *server, Channel *channel) {
    load_channel_policy(server, channel, 0);
}

void chanserv_refresh_channel(Server *server, Channel *channel) {
    load_channel_policy(server, channel, 1);
}

int chanserv_mode_change_allowed(Server *server, const Channel *channel,
                                 ChannelModeSet bit, int adding) {
    int desired;
    (void)server;
    if (channel == NULL || bit == 0U ||
        !channel_mode_has(channel->modes, CHANNEL_MODE_REGISTERED)) return 1;
    if (!channel->chanserv_policy_valid) return 0;
    desired = (channel->chanserv_mode_lock & bit) != 0U ? 1 : 0;
    return desired == (adding ? 1 : 0);
}

void chanserv_persist_channel(Server *server, const Channel *channel) {
    if (server == NULL || channel == NULL ||
        !channel_mode_has(channel->modes, CHANNEL_MODE_REGISTERED)) return;
    (void)chanserv_persist_save(server->config.chanserv_db, channel);
}

int chanserv_client_is_founder(Server *server, const Client *client, const char *channel_name) {
    Channel *channel;
    ChanServDb db = {0};
    ChanServChannel record;
    int result = 0;
    if (server == NULL || client == NULL || channel_name == NULL || client->account_name[0] == '\0') return 0;
    channel = hash_get(&server->channels_by_name, channel_name);
    if (channel != NULL && channel->chanserv_policy_loaded)
        return cached_founder_matches(channel, client);
    if (chanserv_db_open(&db, server->config.chanserv_db) == 0) {
        if (chanserv_db_get(&db, channel_name, &record) == 1 && record.enabled)
            result = founder_matches(&record, client);
        chanserv_db_close(&db);
    }
    return result;
}

ChannelPrivilegeSet chanserv_client_privileges(Server *server, const Client *client,
                                               const char *channel_name) {
    Channel *channel;
    ChanServDb db = {0};
    ChanServChannel channel_record;
    ChanServAccess access;
    ChannelPrivilegeSet privileges = 0U;
    int live_policy = 0;
    if (server == NULL || client == NULL || channel_name == NULL || client->account_name[0] == '\0') return 0U;

    channel = hash_get(&server->channels_by_name, channel_name);
    if (channel != NULL && channel->chanserv_policy_loaded) {
        if (!channel->chanserv_policy_valid) return 0U;
        live_policy = 1;
        if (cached_founder_matches(channel, client))
            return CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR;
    }

    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return 0U;
    if (!live_policy) {
        if (chanserv_db_get(&db, channel_name, &channel_record) != 1 || !channel_record.enabled) {
            chanserv_db_close(&db);
            return 0U;
        }
        if (founder_matches(&channel_record, client)) {
            chanserv_db_close(&db);
            return CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR;
        }
    }

    if (chanserv_db_access_get(&db, channel_name, client->account_name, &access) == 1) {
        switch (access.level) {
            case CHANSERV_ACCESS_OWNER: privileges = CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR; break;
            case CHANSERV_ACCESS_PROTECTED: privileges = CHANNEL_PRIV_PROTECTED | CHANNEL_PRIV_OPERATOR; break;
            case CHANSERV_ACCESS_OP: privileges = CHANNEL_PRIV_OPERATOR; break;
            case CHANSERV_ACCESS_HALFOP: privileges = CHANNEL_PRIV_HALFOP; break;
            case CHANSERV_ACCESS_VOICE: privileges = CHANNEL_PRIV_VOICE; break;
            default: break;
        }
    }
    chanserv_db_close(&db);
    return privileges;
}

static int load_founder_channel(Server *server, Client *client, const char *name,
                                ChanServDb *db, ChanServChannel *record) {
    if (!valid_channel_name(name) || chanserv_db_open(db, server->config.chanserv_db) != 0 ||
        chanserv_db_get(db, name, record) != 1 || !record->enabled) {
        chanserv_db_close(db); cs_notice(server, client, "Channel is not registered."); return 0;
    }
    if (!founder_matches(record, client)) {
        chanserv_db_close(db); cs_notice(server, client, "Only the channel founder may change this setting."); return 0;
    }
    return 1;
}

static void command_register(Server *server, Client *client, char *params) {
    ChanServDb db = {0}; Channel *channel; ChannelMember *member; char *name; char *description;
    if (client->account_name[0] == '\0') { cs_notice(server, client, "You must identify to NickServ before registering a channel."); return; }
    name = params != NULL ? strtok(params, " ") : NULL; description = strtok(NULL, "");
    if (description != NULL) { while (*description == ' ') ++description; if (*description == ':') ++description; }
    if (!valid_channel_name(name)) { cs_notice(server, client, "Syntax: REGISTER <#channel> [:description]"); return; }
    if (description != NULL && strlen(description) > IRCD_CHANSERV_DESCRIPTION_MAX) { cs_notice(server, client, "Channel description is too long."); return; }
    channel = hash_get(&server->channels_by_name, name); member = channel != NULL ? channel_find_member(channel, client) : NULL;
    if (channel == NULL || member == NULL || !channel_privilege_has(member->privileges, CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR)) {
        cs_notice(server, client, "You must be a channel owner or operator to register it."); return;
    }
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0 || chanserv_db_create(&db, channel->name, client->account_name, description != NULL ? description : "") != 0) {
        chanserv_db_close(&db); cs_notice(server, client, "Channel registration failed or the channel is already registered."); return;
    }
    chanserv_db_close(&db);
    channel->chanserv_policy_loaded = 1;
    channel->chanserv_policy_valid = 1;
    (void)snprintf(channel->chanserv_founder, sizeof(channel->chanserv_founder), "%s", client->account_name);
    channel->chanserv_mode_lock = 0U;
    channel->modes = channel_mode_add(channel->modes, CHANNEL_MODE_REGISTERED);
    (void)channel_add_privileges(channel, client, CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR);
    chanserv_persist_channel(server, channel);
    cs_notice(server, client, "Channel registered successfully.");
}

static void command_info(Server *server, Client *client, char *params) {
    ChanServDb db = {0}; ChanServChannel record; char line[IRCD_OUTPUT_BUFFER_SIZE]; char *name=params!=NULL?strtok(params," "):NULL;
    if (!valid_channel_name(name)) { cs_notice(server, client, "Syntax: INFO <#channel>"); return; }
    if (chanserv_db_open(&db,server->config.chanserv_db)!=0 || chanserv_db_get(&db,name,&record)!=1 || !record.enabled) {
        chanserv_db_close(&db); cs_notice(server,client,"Channel is not registered."); return;
    }
    chanserv_db_close(&db);
    (void)snprintf(line,sizeof(line),"Channel %s founder=%s description=%s mlock=0x%llx created=%lld",
        record.name,record.founder,record.description[0]?record.description:"-",(unsigned long long)record.mode_lock,record.created_at);
    cs_notice(server,client,line);
}

static void command_drop(Server *server, Client *client, char *params) {
    ChanServDb db={0}; ChanServChannel record; Channel *channel; char *name=params!=NULL?strtok(params," "):NULL;
    if (!load_founder_channel(server,client,name,&db,&record)) return;
    if (chanserv_db_delete(&db,record.name)!=0) { chanserv_db_close(&db); cs_notice(server,client,"Channel drop failed."); return; }
    chanserv_db_close(&db); channel=hash_get(&server->channels_by_name,record.name);
    if(channel!=NULL) clear_cached_policy(channel);
    cs_notice(server,client,"Channel registration dropped.");
}

static void command_access(Server *server, Client *client, char *params) {
    ChanServDb db={0}; ChanServChannel channel_record; NickServDb nsdb={0}; NickServAccount account;
    char *name=params!=NULL?strtok(params," "):NULL; char *action=strtok(NULL," "); char *account_name; char *level_text;
    if (!load_founder_channel(server,client,name,&db,&channel_record)) return;
    if (action == NULL) { chanserv_db_close(&db); cs_notice(server,client,"Syntax: ACCESS <#channel> ADD|DEL|LIST ..."); return; }
    if (strcasecmp(action,"LIST")==0) {
        char list[IRCD_MESSAGE_BUFFER_SIZE];
        if (chanserv_db_access_list(&db,name,list,sizeof(list))==0) {
            char line[IRCD_OUTPUT_BUFFER_SIZE]; (void)snprintf(line,sizeof(line),"Access %s: %s",name,list[0]?list:"(empty)"); cs_notice(server,client,line);
        }
        chanserv_db_close(&db); return;
    }
    account_name=strtok(NULL," ");
    if (account_name==NULL) { chanserv_db_close(&db); cs_notice(server,client,"Syntax: ACCESS <#channel> ADD <account> <OWNER|PROTECTED|OP|HALFOP|VOICE> or DEL <account>"); return; }
    if (strcasecmp(account_name,channel_record.founder)==0) { chanserv_db_close(&db); cs_notice(server,client,"The founder is implicitly OWNER and cannot be added to the access list."); return; }
    if (strcasecmp(action,"DEL")==0) {
        int rc=chanserv_db_access_delete(&db,name,account_name); chanserv_db_close(&db);
        cs_notice(server,client,rc==0?"Access entry removed.":"No matching access entry."); return;
    }
    level_text=strtok(NULL," ");
    if (strcasecmp(action,"ADD")!=0 || parse_access(level_text)==CHANSERV_ACCESS_NONE) {
        chanserv_db_close(&db); cs_notice(server,client,"Syntax: ACCESS <#channel> ADD <account> <OWNER|PROTECTED|OP|HALFOP|VOICE>"); return;
    }
    if (nickserv_db_open(&nsdb,server->config.nickserv_db)!=0 || nickserv_db_get(&nsdb,account_name,&account)!=1 || !account.enabled) {
        nickserv_db_close(&nsdb); chanserv_db_close(&db); cs_notice(server,client,"NickServ account does not exist or is disabled."); return;
    }
    nickserv_db_close(&nsdb);
    if (chanserv_db_access_set(&db,name,account.name,parse_access(level_text))==0) {
        char line[160]; (void)snprintf(line,sizeof(line),"Access set: %s %s",account.name,access_name(parse_access(level_text))); cs_notice(server,client,line);
    } else cs_notice(server,client,"Failed to update access list.");
    chanserv_db_close(&db);
}

static void command_set(Server *server, Client *client, char *params) {
    ChanServDb db={0}; ChanServChannel record; char *name=params!=NULL?strtok(params," "):NULL; char *field=strtok(NULL," "); char *value=strtok(NULL,"");
    if (!load_founder_channel(server,client,name,&db,&record)) return;
    if (field==NULL || value==NULL) { chanserv_db_close(&db); cs_notice(server,client,"Syntax: SET <#channel> MLOCK <modes> | TOPIC :<text>"); return; }
    while(*value==' ')++value;
    if (strcasecmp(field,"MLOCK")==0) {
        ChannelModeSet lock;
        if (parse_mode_lock(value,&lock)!=0 || chanserv_db_set_mode_lock(&db,name,(uint64_t)lock)!=0) {
            chanserv_db_close(&db); cs_notice(server,client,"Invalid persistent mode lock. Only boolean channel modes are supported."); return;
        }
        chanserv_db_close(&db);
        { Channel *channel=hash_get(&server->channels_by_name,name); if(channel!=NULL)chanserv_refresh_channel(server,channel); }
        cs_notice(server,client,"Persistent mode lock updated."); return;
    }
    if (strcasecmp(field,"TOPIC")==0) {
        char setter[IRC_CHANNEL_TOPIC_SETTER_MAX+1U]; long long now=(long long)time(NULL); Channel *channel;
        if(*value==':')++value;
        if(strlen(value)>IRC_CHANNEL_TOPIC_MAX){chanserv_db_close(&db);cs_notice(server,client,"Topic is too long.");return;}
        (void)snprintf(setter,sizeof(setter),"%s!%s@%s",client->nick,client->user,client->display_host);
        if(chanserv_db_set_topic(&db,name,value,setter,now)!=0){chanserv_db_close(&db);cs_notice(server,client,"Failed to store persistent topic.");return;}
        chanserv_db_close(&db); channel=hash_get(&server->channels_by_name,name); if(channel!=NULL)chanserv_refresh_channel(server,channel);
        cs_notice(server,client,"Persistent topic updated."); return;
    }
    chanserv_db_close(&db); cs_notice(server,client,"Unknown SET field. Use MLOCK or TOPIC.");
}

void chanserv_handle_message(Server *server, Client *client, char *text) {
    char *command; char *params;
    if(server==NULL||client==NULL||text==NULL)return;
    command=strtok(text," "); params=strtok(NULL,""); if(command==NULL)return;
    if(strcasecmp(command,"REGISTER")==0)command_register(server,client,params);
    else if(strcasecmp(command,"INFO")==0)command_info(server,client,params);
    else if(strcasecmp(command,"DROP")==0)command_drop(server,client,params);
    else if(strcasecmp(command,"ACCESS")==0)command_access(server,client,params);
    else if(strcasecmp(command,"SET")==0)command_set(server,client,params);
    else if(strcasecmp(command,"HELP")==0)
        cs_notice(server,client,"Commands: REGISTER, INFO, DROP, ACCESS, SET, HELP");
    else cs_notice(server,client,"Unknown ChanServ command. Use CHANSERV HELP.");
}
