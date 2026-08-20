/**
 * @file zline.c
 * @brief Persistent numeric-IP ZLINE management.
 *
 * ZLINE matches only Client.real_ip. For future WebIRC connections that field
 * is the authenticated end-user address, never the gateway socket address.
 */

#include "ban_db.h"
#include "commands.h"
#include "numerics.h"
#include "oper.h"

#include <string.h>

CommandResult command_zline(Server *server, Client *client, char *params) {
    char *mask;
    char *reason;
    BanDb db = {0};
    size_t i = 0U;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_ZLINE)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "ZLINE");
        return COMMAND_KEEP_CLIENT;
    }

    mask = strtok(params, " ");
    reason = strtok(NULL, "");
    if (mask == NULL || *mask == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "ZLINE");
        return COMMAND_KEEP_CLIENT;
    }

    if (*mask == '-') {
        ++mask;
        if (*mask == '\0' || ban_db_open(&db, server->config.bans_db) != 0 ||
            ban_db_delete(&db, BAN_TYPE_ZLINE, mask) != 0) {
            ban_db_close(&db);
            client_sendf(client, ":%s NOTICE %s :ZLINE removal failed",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        ban_db_close(&db);
        client_sendf(client, ":%s NOTICE %s :ZLINE removed: %s",
                     server->config.server_name, client->nick, mask);
        return COMMAND_KEEP_CLIENT;
    }

    if (reason != NULL && *reason == ':') ++reason;
    if (reason == NULL || *reason == '\0') reason = "Z-lined";

    if (ban_db_open(&db, server->config.bans_db) != 0 ||
        ban_db_add(&db, BAN_TYPE_ZLINE, mask, reason,
                   client->oper_name[0] != '\0' ? client->oper_name : client->nick) != 0) {
        ban_db_close(&db);
        client_sendf(client, ":%s NOTICE %s :ZLINE failed",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    while (i < server->client_count) {
        Client *target = server->clients[i];
        BanRecord match;
        if (target != client && ban_db_match(&db, BAN_TYPE_ZLINE,
                                             target->real_ip, NULL, &match) == 1) {
            client_sendf(target, ERR_YOUREBANNEDCREEP,
                         server->config.server_name,
                         command_reply_nick(target), server->config.admin_email);
            server_disconnect(server, target, reason);
            continue;
        }
        ++i;
    }
    ban_db_close(&db);
    client_sendf(client, ":%s NOTICE %s :ZLINE added: %s",
                 server->config.server_name, client->nick, mask);
    return COMMAND_KEEP_CLIENT;
}
