/**
 * @file zline.c
 * @brief Persistent numeric-IP ZLINE management.
 *
 * ZLINE matches only Client.real_ip. For WebIRC connections that field is the
 * authenticated end-user address, never the gateway socket address.
 *
 * ZLINE <nick> is shorthand for a temporary exact real_ip ban using the
 * configured default duration/reason. Explicit IP masks remain permanent.
 */

#include "ban_db.h"
#include "commands.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

CommandResult command_zline(Server *server, Client *client, char *params) {
    char *mask;
    char *reason;
    char resolved_mask[IRC_IP_MAX + 1U];
    BanDb db = {0};
    size_t i = 0U;
    char notice[IRCD_MESSAGE_BUFFER_SIZE];
    int shorthand = 0;

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
        (void)snprintf(notice, sizeof(notice), "%s removed ZLINE %s", client->nick, mask);
        server_notice_broadcast(server, notice);
        return COMMAND_KEEP_CLIENT;
    }

    if (strchr(mask, '.') == NULL && strchr(mask, ':') == NULL && strchr(mask, '*') == NULL && strchr(mask, '?') == NULL) {
        Client *target = hash_get(&server->clients_by_nick, mask);
        if (target == NULL) {
            client_sendf(client, ERR_NOSUCHNICK, server->config.server_name,
                         client->nick, mask);
            return COMMAND_KEEP_CLIENT;
        }
        (void)snprintf(resolved_mask, sizeof(resolved_mask), "%s", target->real_ip);
        mask = resolved_mask;
        reason = server->config.zline_default_reason;
        shorthand = 1;
    } else {
        if (reason != NULL && *reason == ':') ++reason;
        if (reason == NULL || *reason == '\0') reason = "Z-lined";
    }

    if (ban_db_open(&db, server->config.bans_db) != 0 ||
        (shorthand
            ? ban_db_add_timed(&db, BAN_TYPE_ZLINE, mask, reason,
                               client->oper_name[0] != '\0' ? client->oper_name : client->nick,
                               server->config.zline_default_duration_seconds)
            : ban_db_add(&db, BAN_TYPE_ZLINE, mask, reason,
                         client->oper_name[0] != '\0' ? client->oper_name : client->nick)) != 0) {
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

    if (shorthand) {
        client_sendf(client, ":%s NOTICE %s :ZLINE added: %s (%us, %s)",
                     server->config.server_name, client->nick, mask,
                     server->config.zline_default_duration_seconds, reason);
        (void)snprintf(notice, sizeof(notice), "%s added temporary ZLINE %s for %us (%s)",
                       client->nick, mask, server->config.zline_default_duration_seconds, reason);
    } else {
        client_sendf(client, ":%s NOTICE %s :ZLINE added: %s",
                     server->config.server_name, client->nick, mask);
        (void)snprintf(notice, sizeof(notice), "%s added ZLINE %s (%s)",
                       client->nick, mask, reason);
    }
    server_notice_broadcast(server, notice);
    return COMMAND_KEEP_CLIENT;
}
