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
#include "irc.h"
#include "modes.h"
#include "nickserv_db.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void cs_notice(Server *server, Client *client, const char *text) {
    int prefix_length;
    size_t payload_limit;
    size_t text_length;
    size_t offset = 0U;

    if (server == NULL || client == NULL || text == NULL) return;
    prefix_length = snprintf(NULL, 0, ":ChanServ!service@%s NOTICE %s :",
                             server->config.server_name, client->nick);
    if (prefix_length < 0 || (size_t)prefix_length >= IRC_LINE_CONTENT_MAX)
        return;
    payload_limit = IRC_LINE_CONTENT_MAX - (size_t)prefix_length;
    text_length = strlen(text);

    if (text_length == 0U) {
        client_sendf(client, ":ChanServ!service@%s NOTICE %s :",
                     server->config.server_name, client->nick);
        return;
    }

    while (offset < text_length && !client->output_overflowed) {
        size_t remaining = text_length - offset;
        size_t chunk = remaining < payload_limit ? remaining : payload_limit;
        client_sendf(client, ":ChanServ!service@%s NOTICE %s :%.*s",
                     server->config.server_name, client->nick,
                     (int)chunk, text + offset);
        offset += chunk;
    }
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
    /* A dropped/disabled registration stops future service reconciliation,
     * but does not rewrite the ordinary live channel's current modes. */
    channel_forget_service_privileges(channel);
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

typedef struct AccessListNoticeContext {
    Server *server;
    Client *client;
    const char *channel;
    size_t count;
} AccessListNoticeContext;

static int access_list_notice(const ChanServAccess *record, void *context) {
    AccessListNoticeContext *list = context;
    char line[160];
    int written;
    if (record == NULL || list == NULL || list->server == NULL ||
        list->client == NULL || list->channel == NULL) return -1;
    written = snprintf(line, sizeof(line), "Access %s: %s:%d",
                       list->channel, record->account, (int)record->level);
    if (written < 0 || (size_t)written >= sizeof(line)) return -1;
    cs_notice(list->server, list->client, line);
    ++list->count;
    return 0;
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
        size_t topic_limit = irc_topic_limit(server);
        cache_policy(channel, &record);
        channel->modes = (ChannelModeSet)record.mode_lock;
        channel->modes = channel_mode_add(channel->modes, CHANNEL_MODE_REGISTERED);
        if (strlen(record.topic) <= topic_limit) {
            (void)snprintf(channel->topic, sizeof(channel->topic), "%s", record.topic);
            (void)snprintf(channel->topic_setter, sizeof(channel->topic_setter), "%s", record.topic_setter);
            channel->topic_time = (time_t)record.topic_time;
        } else {
            channel->topic[0] = '\0';
            channel->topic_setter[0] = '\0';
            channel->topic_time = (time_t)0;
            fprintf(stderr,
                    "ChanServ: persistent topic for %s is %zu bytes, above active TOPICLEN=%zu; live topic left unset\n",
                    record.name, strlen(record.topic), topic_limit);
        }
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

static void broadcast_privilege_change(Server *server, Channel *channel,
                                       Client *subject,
                                       ChannelPrivilegeSet privileges,
                                       char sign) {
    ChannelMember *recipient;
    char letters[6];
    char parameters[(IRC_NICK_MAX + 2U) * 5U + 1U] = "";
    size_t used = 0U;
    size_t i;

    if (server == NULL || channel == NULL || subject == NULL || privileges == 0U)
        return;
    (void)channel_privilege_format(privileges, letters, sizeof(letters));
    for (i = 0U; letters[i] != '\0'; ++i) {
        int written = snprintf(parameters + used, sizeof(parameters) - used,
                               " %s", subject->nick);
        if (written < 0 || (size_t)written >= sizeof(parameters) - used) return;
        used += (size_t)written;
    }
    for (recipient = channel->members; recipient != NULL; recipient = recipient->next)
        client_sendf(recipient->client, ":%s MODE %s %c%s%s",
                     server->config.server_name, channel->name,
                     sign, letters, parameters);
}

static void sync_member_privileges(Server *server, Channel *channel,
                                   Client *client) {
    ChannelMember *member;
    ChannelPrivilegeSet before;
    ChannelPrivilegeSet after;
    ChannelPrivilegeSet desired;

    if (server == NULL || channel == NULL || client == NULL) return;
    member = channel_find_member(channel, client);
    if (member == NULL) return;
    before = member->privileges;
    desired = channel_mode_has(channel->modes, CHANNEL_MODE_REGISTERED)
                  ? chanserv_client_privileges(server, client, channel->name)
                  : 0U;
    if (channel_set_service_privileges(channel, client, desired) != 0) return;
    after = member->privileges;
    broadcast_privilege_change(server, channel, client, before & ~after, '-');
    broadcast_privilege_change(server, channel, client, after & ~before, '+');
}

void chanserv_sync_client_privileges(Server *server, Client *client) {
    ClientChannelLink *link;
    if (server == NULL || client == NULL) return;
    for (link = client->channels; link != NULL; link = link->next)
        sync_member_privileges(server, link->channel, client);
}

static void sync_account_privileges(Server *server, Channel *channel,
                                    const char *account_name) {
    ChannelMember *member;
    ChannelMember *next;
    if (server == NULL || channel == NULL || account_name == NULL) return;
    for (member = channel->members; member != NULL; member = next) {
        next = member->next;
        if (member->client->account_name[0] != '\0' &&
            strcasecmp(member->client->account_name, account_name) == 0)
            sync_member_privileges(server, channel, member->client);
    }
}

void chanserv_sync_channel_privileges(Server *server, Channel *channel) {
    ChannelMember *member;
    ChannelMember *next;
    if (server == NULL || channel == NULL) return;
    for (member = channel->members; member != NULL; member = next) {
        next = member->next;
        sync_member_privileges(server, channel, member->client);
    }
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
    (void)channel_set_service_privileges(channel, client,
                                         CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR);
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
        AccessListNoticeContext context = {server, client, name, 0U};
        char line[128];
        int rc = chanserv_db_access_foreach(&db, name, access_list_notice, &context);
        if (rc == 0) {
            if (context.count == 0U) {
                (void)snprintf(line, sizeof(line), "Access %s: (empty)", name);
                cs_notice(server, client, line);
            }
            (void)snprintf(line, sizeof(line), "End of access list for %s.", name);
            cs_notice(server, client, line);
        } else {
            cs_notice(server, client, "Failed to list access entries.");
        }
        chanserv_db_close(&db); return;
    }
    account_name=strtok(NULL," ");
    if (account_name==NULL) { chanserv_db_close(&db); cs_notice(server,client,"Syntax: ACCESS <#channel> ADD <account> <OWNER|PROTECTED|OP|HALFOP|VOICE> or DEL <account>"); return; }
    if (strcasecmp(account_name,channel_record.founder)==0) { chanserv_db_close(&db); cs_notice(server,client,"The founder is implicitly OWNER and cannot be added to the access list."); return; }
    if (strcasecmp(action,"DEL")==0) {
        int rc=chanserv_db_access_delete(&db,name,account_name); chanserv_db_close(&db);
        if (rc == 0) {
            Channel *channel = hash_get(&server->channels_by_name, name);
            if (channel != NULL)
                sync_account_privileges(server, channel, account_name);
        }
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
        Channel *channel = hash_get(&server->channels_by_name, name);
        char line[160];
        chanserv_db_close(&db);
        if (channel != NULL)
            sync_account_privileges(server, channel, account.name);
        (void)snprintf(line,sizeof(line),"Access set: %s %s",account.name,access_name(parse_access(level_text))); cs_notice(server,client,line);
    } else {
        chanserv_db_close(&db);
        cs_notice(server,client,"Failed to update access list.");
    }
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
        if(strlen(value)>irc_topic_limit(server)){chanserv_db_close(&db);cs_notice(server,client,"Topic is too long for this server's advertised TOPICLEN.");return;}
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
