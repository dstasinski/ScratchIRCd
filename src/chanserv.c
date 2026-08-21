/**
 * @file chanserv.c
 * @brief Virtual ChanServ service for persistent channel registration.
 *
 * ChanServ never exists as a Client and never joins channels. Registration
 * authority is bound to authenticated NickServ account names.
 */
#include "chanserv.h"
#include "chanserv_db.h"
#include "modes.h"
#include "nickserv_db.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

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

void chanserv_restore_channel(Server *server, Channel *channel) {
    ChanServDb db = {0};
    ChanServChannel record;
    if (server == NULL || channel == NULL) return;
    if (chanserv_db_open(&db, server->config.chanserv_db) == 0) {
        if (chanserv_db_get(&db, channel->name, &record) == 1 && record.enabled)
            channel->modes = channel_mode_add(channel->modes, CHANNEL_MODE_REGISTERED);
        chanserv_db_close(&db);
    }
}

int chanserv_client_is_founder(Server *server, const Client *client, const char *channel_name) {
    ChanServDb db = {0};
    ChanServChannel record;
    int result = 0;
    if (server == NULL || client == NULL || channel_name == NULL || client->account_name[0] == '\0') return 0;
    if (chanserv_db_open(&db, server->config.chanserv_db) == 0) {
        if (chanserv_db_get(&db, channel_name, &record) == 1 && record.enabled)
            result = founder_matches(&record, client);
        chanserv_db_close(&db);
    }
    return result;
}

static void command_register(Server *server, Client *client, char *params) {
    ChanServDb db = {0};
    Channel *channel;
    ChannelMember *member;
    char *name;
    char *description;

    if (client->account_name[0] == '\0') {
        cs_notice(server, client, "You must identify to NickServ before registering a channel.");
        return;
    }
    name = params != NULL ? strtok(params, " ") : NULL;
    description = strtok(NULL, "");
    if (description != NULL) {
        while (*description == ' ') ++description;
        if (*description == ':') ++description;
    }
    if (!valid_channel_name(name)) {
        cs_notice(server, client, "Syntax: REGISTER <#channel> [:description]");
        return;
    }
    if (description != NULL && strlen(description) > IRCD_CHANSERV_DESCRIPTION_MAX) {
        cs_notice(server, client, "Channel description is too long.");
        return;
    }
    channel = hash_get(&server->channels_by_name, name);
    member = channel != NULL ? channel_find_member(channel, client) : NULL;
    if (channel == NULL || member == NULL ||
        !channel_privilege_has(member->privileges, CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR)) {
        cs_notice(server, client, "You must be a channel owner or operator to register it.");
        return;
    }
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0 ||
        chanserv_db_create(&db, channel->name, client->account_name,
                           description != NULL ? description : "") != 0) {
        chanserv_db_close(&db);
        cs_notice(server, client, "Channel registration failed or the channel is already registered.");
        return;
    }
    chanserv_db_close(&db);
    channel->modes = channel_mode_add(channel->modes, CHANNEL_MODE_REGISTERED);
    (void)channel_add_privileges(channel, client, CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR);
    cs_notice(server, client, "Channel registered successfully.");
}

static void command_info(Server *server, Client *client, char *params) {
    ChanServDb db = {0};
    ChanServChannel record;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    char *name = params != NULL ? strtok(params, " ") : NULL;
    if (!valid_channel_name(name)) {
        cs_notice(server, client, "Syntax: INFO <#channel>");
        return;
    }
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0 ||
        chanserv_db_get(&db, name, &record) != 1 || !record.enabled) {
        chanserv_db_close(&db);
        cs_notice(server, client, "Channel is not registered.");
        return;
    }
    chanserv_db_close(&db);
    (void)snprintf(line, sizeof(line), "Channel %s founder=%s description=%s created=%lld",
                   record.name, record.founder,
                   record.description[0] != '\0' ? record.description : "-",
                   record.created_at);
    cs_notice(server, client, line);
}

static void command_drop(Server *server, Client *client, char *params) {
    ChanServDb db = {0};
    ChanServChannel record;
    Channel *channel;
    char *name = params != NULL ? strtok(params, " ") : NULL;
    if (!valid_channel_name(name)) {
        cs_notice(server, client, "Syntax: DROP <#channel>");
        return;
    }
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0 ||
        chanserv_db_get(&db, name, &record) != 1 || !record.enabled) {
        chanserv_db_close(&db);
        cs_notice(server, client, "Channel is not registered.");
        return;
    }
    if (!founder_matches(&record, client)) {
        chanserv_db_close(&db);
        cs_notice(server, client, "Only the channel founder may drop this registration.");
        return;
    }
    if (chanserv_db_delete(&db, record.name) != 0) {
        chanserv_db_close(&db);
        cs_notice(server, client, "Channel drop failed.");
        return;
    }
    chanserv_db_close(&db);
    channel = hash_get(&server->channels_by_name, record.name);
    if (channel != NULL)
        channel->modes = channel_mode_remove(channel->modes, CHANNEL_MODE_REGISTERED);
    cs_notice(server, client, "Channel registration dropped.");
}

void chanserv_handle_message(Server *server, Client *client, char *text) {
    char *command;
    char *params;
    if (server == NULL || client == NULL || text == NULL) return;
    command = strtok(text, " ");
    params = strtok(NULL, "");
    if (command == NULL) return;
    if (strcasecmp(command, "REGISTER") == 0) command_register(server, client, params);
    else if (strcasecmp(command, "INFO") == 0) command_info(server, client, params);
    else if (strcasecmp(command, "DROP") == 0) command_drop(server, client, params);
    else if (strcasecmp(command, "HELP") == 0)
        cs_notice(server, client, "Commands: REGISTER <#channel> [:description], INFO <#channel>, DROP <#channel>, HELP");
    else cs_notice(server, client, "Unknown ChanServ command. Use CHANSERV HELP.");
}
