/**
 * @file chanserv_admin.c
 * @brief Network-administrator management of registered ChanServ channels.
 */
#include "commands.h"
#include "chanserv.h"
#include "chanserv_db.h"
#include "message_policy.h"
#include "modes.h"
#include "nickserv_db.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static int require_netadmin(Server *server, Client *client) {
    if (!client_mode_has(client->modes, CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return 1;
    }
    return 0;
}

static void notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":%s NOTICE %s :%s", server->config.server_name, client->nick, text);
}

CommandResult command_csinfo(Server *server, Client *client, char *params) {
    ChanServDb db = {0};
    ChanServChannel record;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    char *name;
    if (command_require_registered(client) || require_netadmin(server, client)) return COMMAND_KEEP_CLIENT;
    name = params != NULL ? strtok(params, " ") : NULL;
    if (name == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name, client->nick, "CSINFO");
        return COMMAND_KEEP_CLIENT;
    }
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0 ||
        chanserv_db_get(&db, name, &record) != 1) {
        chanserv_db_close(&db);
        notice(server, client, "No such ChanServ channel.");
        return COMMAND_KEEP_CLIENT;
    }
    chanserv_db_close(&db);
    (void)snprintf(line, sizeof(line),
                   "CHANSERV %s founder=%s enabled=%d description=%s created=%lld updated=%lld",
                   record.name, record.founder, record.enabled,
                   record.description[0] != '\0' ? record.description : "-",
                   record.created_at, record.updated_at);
    notice(server, client, line);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_csset(Server *server, Client *client, char *params) {
    ChanServDb db = {0};
    char *name;
    char *field;
    char *value;
    int rc = -1;
    if (command_require_registered(client) || require_netadmin(server, client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name, client->nick, "CSSET");
        return COMMAND_KEEP_CLIENT;
    }
    name = strtok(params, " ");
    field = strtok(NULL, " ");
    value = strtok(NULL, "");
    if (value != NULL) while (*value == ' ') ++value;
    if (name == NULL || field == NULL || value == NULL || *value == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name, client->nick, "CSSET");
        return COMMAND_KEEP_CLIENT;
    }
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) {
        notice(server, client, "CSSET failed.");
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(field, "DESCRIPTION") == 0) {
        if (*value == ':') ++value;
        if (strlen(value) <= IRCD_CHANSERV_DESCRIPTION_MAX)
            rc = chanserv_db_set_description(&db, name, value);
    } else if (strcasecmp(field, "ENABLED") == 0) {
        if (strcmp(value, "0") == 0 || strcmp(value, "1") == 0)
            rc = chanserv_db_set_enabled(&db, name, value[0] == '1');
    } else if (strcasecmp(field, "FOUNDER") == 0) {
        NickServDb nsdb = {0};
        NickServAccount account;
        if (nickserv_db_open(&nsdb, server->config.nickserv_db) == 0) {
            if (nickserv_db_get(&nsdb, value, &account) == 1 && account.enabled)
                rc = chanserv_db_set_founder(&db, name, account.name);
            nickserv_db_close(&nsdb);
        }
    }
    chanserv_db_close(&db);
    if (rc == 0) {
        Channel *channel = hash_get(&server->channels_by_name, name);
        if (channel != NULL)
            chanserv_restore_channel(server, channel);
        snotice_broadcast(server, SNOTICE_SERVICES,
                          "CSSET by %s: channel=%s field=%s value=%s",
                          client->nick, name, field, value);
    }
    notice(server, client, rc == 0 ? "ChanServ channel updated." : "CSSET failed.");
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_csdrop(Server *server, Client *client, char *params) {
    ChanServDb db = {0};
    char *name;
    int rc;
    if (command_require_registered(client) || require_netadmin(server, client)) return COMMAND_KEEP_CLIENT;
    name = params != NULL ? strtok(params, " ") : NULL;
    if (name == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name, client->nick, "CSDROP");
        return COMMAND_KEEP_CLIENT;
    }
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) {
        notice(server, client, "CSDROP failed.");
        return COMMAND_KEEP_CLIENT;
    }
    rc = chanserv_db_delete(&db, name);
    chanserv_db_close(&db);
    if (rc == 0) {
        Channel *channel = hash_get(&server->channels_by_name, name);
        if (channel != NULL)
            chanserv_restore_channel(server, channel);
        snotice_broadcast(server, SNOTICE_SERVICES,
                          "CSDROP by %s: channel=%s", client->nick, name);
    }
    notice(server, client, rc == 0 ? "ChanServ channel deleted." : "CSDROP failed.");
    return COMMAND_KEEP_CLIENT;
}
