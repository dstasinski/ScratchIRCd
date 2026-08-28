/** @file chanserv.c @brief Direct /CHANSERV command alias. */
#include "commands.h"
#include "channel_log.h"
#include "chanserv.h"
#include "chanserv_db.h"
#include "message_policy.h"
#include "modes.h"
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
    int is_register;
    int is_drop;
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
    is_register = service_command != NULL && strcasecmp(service_command, "REGISTER") == 0;
    is_drop = service_command != NULL && strcasecmp(service_command, "DROP") == 0;

    /* Creating and deleting persistent channel registrations is network-wide
     * administrative policy. Founder/access privileges govern an existing
     * registration, but they never grant authority to create or destroy one. */
    if ((is_register || is_drop) &&
        !client_mode_has(client->modes, CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    track_registration = channel_name != NULL && (is_register || is_drop);
    if (track_registration)
        existed_before = chanserv_registration_exists(server, channel_name);

    /* DROP intentionally uses the existing netadmin administrative primitive:
     * a network administrator may remove any registration even after FOUNDER
     * has been assigned to another NickServ account. */
    if (is_drop) {
        char *drop_params = strchr(params, ' ');
        if (drop_params != NULL) {
            while (*drop_params == ' ') ++drop_params;
        }
        (void)command_csdrop(server, client, drop_params);
    } else if (!channel_log_handle_chanserv(server, client, params)) {
        chanserv_handle_message(server, client, params);
    }

    if (track_registration) {
        existed_after = chanserv_registration_exists(server, channel_name);
        if (!existed_before && existed_after && is_register) {
            snotice_broadcast(server, SNOTICE_REGISTRATIONS,
                              "ChanServ registration: channel=%s founder=%s",
                              channel_name,
                              client->account_name[0] != '\0' ? client->account_name : client->nick);
        } else if (existed_before && !existed_after && is_drop) {
            snotice_broadcast(server, SNOTICE_REGISTRATIONS,
                              "ChanServ registration dropped: channel=%s by=%s",
                              channel_name,
                              client->account_name[0] != '\0' ? client->account_name : client->nick);
        }
    }
    return COMMAND_KEEP_CLIENT;
}
