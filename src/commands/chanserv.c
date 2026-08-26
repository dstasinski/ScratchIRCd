/** @file chanserv.c @brief Direct /CHANSERV command alias. */
#include "commands.h"
#include "channel_log.h"
#include "chanserv.h"
#include "chanserv_db.h"
#include "message_policy.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

static int chanserv_registration_exists(Server *server, const char *name) {
    ChanServDb db = {0};
    ChanServChannel record;
    int found = 0;
    if (server == NULL || name == NULL || *name == '\0') return 0;
    if (chanserv_db_open(&db, server->config.chanserv_db) == 0) {
        found = chanserv_db_get(&db, name, &record) == 1;
        chanserv_db_close(&db);
    }
    return found;
}

static int founder_registration_count(Server *server, const char *founder,
                                      size_t *count) {
    ChanServDb db = {0};
    sqlite3_stmt *stmt = NULL;
    int rc = -1;
    if (count != NULL) *count = 0U;
    if (server == NULL || founder == NULL || *founder == '\0' || count == NULL)
        return -1;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return -1;
    if (sqlite3_prepare_v2(db.db,
            "SELECT COUNT(*) FROM channels WHERE founder=?1 COLLATE NOCASE",
            -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, founder, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            sqlite3_int64 value = sqlite3_column_int64(stmt, 0);
            if (value >= 0) { *count = (size_t)value; rc = 0; }
        }
    }
    if (stmt != NULL) sqlite3_finalize(stmt);
    chanserv_db_close(&db);
    return rc;
}

CommandResult command_chanserv(Server *server, Client *client, char *params) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *service_command;
    char *channel_name;
    int track_registration;
    int existed_before = 0;
    int existed_after;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "CHANSERV");
        return COMMAND_KEEP_CLIENT;
    }

    (void)snprintf(copy, sizeof(copy), "%s", params);
    service_command = strtok(copy, " ");
    channel_name = service_command != NULL ? strtok(NULL, " ") : NULL;
    track_registration = service_command != NULL && channel_name != NULL &&
                         (strcasecmp(service_command, "REGISTER") == 0 ||
                          strcasecmp(service_command, "DROP") == 0);
    if (track_registration)
        existed_before = chanserv_registration_exists(server, channel_name);

    if (service_command != NULL && channel_name != NULL &&
        strcasecmp(service_command, "REGISTER") == 0 && !existed_before &&
        client->account_name[0] != '\0' &&
        !client_mode_has(client->modes, CLIENT_MODE_NETADMIN) &&
        server->config.chanserv_max_channels_per_account != 0U) {
        size_t owned = 0U;
        if (founder_registration_count(server, client->account_name, &owned) != 0) {
            client_sendf(client, ":ChanServ!service@%s NOTICE %s :Unable to verify your channel registration quota.",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (owned >= server->config.chanserv_max_channels_per_account) {
            client_sendf(client, ":ChanServ!service@%s NOTICE %s :You have reached the maximum of %u registered channels for one account.",
                         server->config.server_name, client->nick,
                         server->config.chanserv_max_channels_per_account);
            snotice_broadcast(server, SNOTICE_REGISTRATIONS | SNOTICE_FLOOD,
                              "ChanServ registration quota reached: account=%s owned=%zu limit=%u requested=%s",
                              client->account_name, owned,
                              server->config.chanserv_max_channels_per_account,
                              channel_name);
            return COMMAND_KEEP_CLIENT;
        }
    }

    if (!channel_log_handle_chanserv(server, client, params))
        chanserv_handle_message(server, client, params);

    if (track_registration) {
        existed_after = chanserv_registration_exists(server, channel_name);
        if (!existed_before && existed_after && strcasecmp(service_command, "REGISTER") == 0) {
            snotice_broadcast(server, SNOTICE_REGISTRATIONS,
                              "ChanServ registration: channel=%s founder=%s",
                              channel_name,
                              client->account_name[0] != '\0' ? client->account_name : client->nick);
        } else if (existed_before && !existed_after && strcasecmp(service_command, "DROP") == 0) {
            snotice_broadcast(server, SNOTICE_REGISTRATIONS,
                              "ChanServ registration dropped: channel=%s by=%s",
                              channel_name,
                              client->account_name[0] != '\0' ? client->account_name : client->nick);
        }
    }
    return COMMAND_KEEP_CLIENT;
}
