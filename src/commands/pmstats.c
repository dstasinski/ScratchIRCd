/** @file pmstats.c @brief Private per-client PMSTATS flag control. */
#include "commands.h"
#include "modes.h"
#include "oper.h"

#include <string.h>
#include <strings.h>

static int pmstats_authorized(const Client *client)
{
    if (client == NULL) return 0;
    if (!client_mode_has(client->modes, CLIENT_MODE_OPER)) return 0;
    if (!client_mode_has(client->modes, CLIENT_MODE_BOT)) return 0;
    return oper_permission_has(client->oper_permissions, OPER_PERMISSION_STATS);
}

CommandResult command_pmstats(Server *server, Client *client, char *params)
{
    char *value;
    char *end;

    if (server == NULL || client == NULL) return COMMAND_KEEP_CLIENT;

    /* Deliberately hide this command from clients that are not eligible to
     * use it. They receive the same reply they would receive if PMSTATS were
     * not present in the command table at all. */
    if (!pmstats_authorized(client)) {
        client_sendf(client, ":%s 421 %s PMSTATS :Unknown command",
                     server->config.server_name, command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }

    value = params;
    if (value == NULL) value = "";
    while (*value == ' ') ++value;
    end = value;
    while (*end != '\0' && *end != ' ') ++end;
    while (*end == ' ') ++end;

    if (*value == '\0' || *end != '\0') {
        client_sendf(client, ":%s 461 %s PMSTATS :Not enough parameters",
                     server->config.server_name, command_reply_nick(client));
        return COMMAND_KEEP_CLIENT;
    }

    if (strcasecmp(value, "on") == 0) {
        client->pmstats_enabled = 1;
    } else if (strcasecmp(value, "off") == 0) {
        client->pmstats_enabled = 0;
    } else {
        client_sendf(client, ":%s 461 %s PMSTATS :Expected ON or OFF",
                     server->config.server_name, command_reply_nick(client));
    }

    return COMMAND_KEEP_CLIENT;
}
