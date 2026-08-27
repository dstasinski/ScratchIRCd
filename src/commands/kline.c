/**
 * @file kline.c
 * @brief Persistent user@host KLINE management.
 *
 * KLINE is server-security policy. It matches the client's actual FCrDNS host
 * and actual numeric IP, never display_host. Cloaks and vhosts therefore do
 * not affect KLINE matching.
 *
 * KLINE <nick> is shorthand for a temporary *@real_host ban (falling back to
 * *@real_ip) using the configured default duration/reason. Explicit user@host
 * masks retain the existing permanent-ban behavior.
 */

#include "ban_db.h"
#include "commands.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

static int client_matches_kline(BanDb *db, const Client *target, BanRecord *record) {
    char host_identity[IRCD_MESSAGE_BUFFER_SIZE];
    char ip_identity[IRCD_MESSAGE_BUFFER_SIZE];
    const char *first;

    if (target->real_host[0] != '\0') {
        (void)snprintf(host_identity, sizeof(host_identity), "%s@%s",
                       target->user, target->real_host);
        first = host_identity;
    } else {
        first = ip_identity;
    }

    (void)snprintf(ip_identity, sizeof(ip_identity), "%s@%s",
                   target->user, target->real_ip);
    if (target->real_host[0] == '\0') first = ip_identity;

    return ban_db_match(db, BAN_TYPE_KLINE, first, ip_identity, record);
}

CommandResult command_kline(Server *server, Client *client, char *params) {
    char *mask;
    char *reason;
    char resolved_mask[IRC_CHANNEL_MASK_MAX + 1U];
    BanDb db = {0};
    size_t i = 0U;
    int shorthand = 0;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "KLINE");
        return COMMAND_KEEP_CLIENT;
    }

    mask = strtok(params, " ");
    reason = strtok(NULL, "");
    if (mask == NULL || *mask == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "KLINE");
        return COMMAND_KEEP_CLIENT;
    }

    if (*mask == '-') {
        if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_UNKLINE)) {
            client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        ++mask;
        if (*mask == '\0' || ban_db_open(&db, server->config.bans_db) != 0 ||
            ban_db_delete(&db, BAN_TYPE_KLINE, mask) != 0) {
            ban_db_close(&db);
            client_sendf(client, ":%s NOTICE %s :KLINE removal failed",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        ban_db_close(&db);
        client_sendf(client, ":%s NOTICE %s :KLINE removed: %s",
                     server->config.server_name, client->nick, mask);
        snotice_broadcast(server, SNOTICE_BANS, "%s removed KLINE %s",
                          client->nick, mask);
        return COMMAND_KEEP_CLIENT;
    }

    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_KLINE)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    if (strchr(mask, '@') == NULL) {
        Client *target = hash_get(&server->clients_by_nick, mask);
        const char *host;
        int written;
        if (target == NULL) {
            client_sendf(client, ERR_NOSUCHNICK, server->config.server_name,
                         client->nick, mask);
            return COMMAND_KEEP_CLIENT;
        }
        host = target->real_host[0] != '\0' ? target->real_host : target->real_ip;
        written = snprintf(resolved_mask, sizeof(resolved_mask), "*@%s", host);
        if (written < 0 || (size_t)written >= sizeof(resolved_mask)) {
            client_sendf(client,
                         ":%s NOTICE %s :KLINE failed: resolved host is too long for a ban mask",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        mask = resolved_mask;
        reason = server->config.kline_default_reason;
        shorthand = 1;
    } else {
        if (reason != NULL && *reason == ':') ++reason;
        if (reason == NULL || *reason == '\0') reason = "K-lined";
    }

    if (ban_db_open(&db, server->config.bans_db) != 0 ||
        (shorthand
            ? ban_db_add_timed(&db, BAN_TYPE_KLINE, mask, reason,
                               client->oper_name[0] != '\0' ? client->oper_name : client->nick,
                               server->config.kline_default_duration_seconds)
            : ban_db_add(&db, BAN_TYPE_KLINE, mask, reason,
                         client->oper_name[0] != '\0' ? client->oper_name : client->nick)) != 0) {
        ban_db_close(&db);
        client_sendf(client, ":%s NOTICE %s :KLINE failed",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    while (i < server->client_count) {
        Client *target = server->clients[i];
        BanRecord match;
        if (target != client && client_matches_kline(&db, target, &match) == 1) {
            snotice_broadcast(server, SNOTICE_BANS,
                              "KLINE matched %s (%s@%s) [real_ip=%s] by %s",
                              command_reply_nick(target), target->user,
                              target->display_host, target->real_ip, mask);
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
        client_sendf(client, ":%s NOTICE %s :KLINE added: %s (%us, %s)",
                     server->config.server_name, client->nick, mask,
                     server->config.kline_default_duration_seconds, reason);
        snotice_broadcast(server, SNOTICE_BANS,
                          "%s added temporary KLINE %s for %us (%s)",
                          client->nick, mask,
                          server->config.kline_default_duration_seconds, reason);
    } else {
        client_sendf(client, ":%s NOTICE %s :KLINE added: %s",
                     server->config.server_name, client->nick, mask);
        snotice_broadcast(server, SNOTICE_BANS, "%s added KLINE %s (%s)",
                          client->nick, mask, reason);
    }
    return COMMAND_KEEP_CLIENT;
}
