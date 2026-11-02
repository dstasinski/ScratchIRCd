/** @file vacuum.c @brief Network-administrator database maintenance command.
 *
 * Initial read-only command surface. VACUUM execution is deliberately not
 * exposed until a safe background worker and shutdown coordination exist.
 */
#include "commands.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

typedef struct VacuumDatabase {
    const char *name;
    const char *path;
} VacuumDatabase;

static void vacuum_notice(Server *server, Client *client, const char *message) {
    client_sendf(client, ":%s NOTICE %s :VACUUM %s",
                 server->config.server_name, client->nick, message);
}

CommandResult command_vacuum(Server *server, Client *client, char *params) {
    VacuumDatabase databases[6];
    char option[32];
    size_t i = 0U;
    const char *cursor = params != NULL ? params : "";
    const char *end;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor == ':') ++cursor;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    end = cursor;
    while (*end != '\0' && *end != ' ' && *end != '\t') ++end;
    if ((size_t)(end - cursor) >= sizeof(option)) {
        vacuum_notice(server, client, "Syntax: /VACUUM LIST or /VACUUM STATUS");
        return COMMAND_KEEP_CLIENT;
    }
    memcpy(option, cursor, (size_t)(end - cursor));
    option[end - cursor] = '\0';
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0') {
        vacuum_notice(server, client, "Syntax: /VACUUM LIST or /VACUUM STATUS");
        return COMMAND_KEEP_CLIENT;
    }

    if (strcasecmp(option, "STATUS") == 0) {
        vacuum_notice(server, client, "idle; execution not yet implemented");
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(option, "LIST") != 0) {
        vacuum_notice(server, client,
                      "Execution unavailable; use /VACUUM LIST or /VACUUM STATUS");
        return COMMAND_KEEP_CLIENT;
    }

    databases[0] = (VacuumDatabase){"NICKSERV", server->config.nickserv_db};
    databases[1] = (VacuumDatabase){"CHANSERV", server->config.chanserv_db};
    databases[2] = (VacuumDatabase){"MEMOSERV", server->config.memoserv_db};
    databases[3] = (VacuumDatabase){"OPERATORS", server->config.operators_db};
    databases[4] = (VacuumDatabase){"BANS", server->config.bans_db};
    databases[5] = (VacuumDatabase){"HISTORY", server->config.history_db};

    for (i = 0U; i < sizeof(databases) / sizeof(databases[0]); ++i) {
        client_sendf(client, ":%s NOTICE %s :VACUUM %s: %s",
                     server->config.server_name, client->nick,
                     databases[i].name, databases[i].path);
        if (client->output_overflowed) break;
    }
    return COMMAND_KEEP_CLIENT;
}
