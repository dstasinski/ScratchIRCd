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
