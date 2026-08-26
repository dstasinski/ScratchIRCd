/**
 * @file zline.c
 * @brief Persistent numeric-IP and CIDR ZLINE management.
 *
 * ZLINE matches only Client.real_ip. For WebIRC connections that field is the
 * authenticated end-user address, never the gateway socket address.
 *
 * ZLINE <nick> is shorthand for a temporary exact real_ip ban using the
 * configured default duration/reason. Explicit masks may be exact IPv4/IPv6,
 * CIDR, or legacy wildcard IP masks.
 */

#include "ban_db.h"
#include "commands.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int valid_cidr_mask(const char *mask) {
    char address[IRC_IP_MAX + 1U];
    const char *slash;
    char *end = NULL;
    unsigned long prefix;
    struct in_addr v4;
    struct in6_addr v6;
    unsigned long max_prefix;

    if (mask == NULL || (slash = strchr(mask, '/')) == NULL || slash == mask ||
        strchr(slash + 1, '/') != NULL || (size_t)(slash - mask) >= sizeof(address)) return 0;
    memcpy(address, mask, (size_t)(slash - mask));
    address[slash - mask] = '\0';

    if (inet_pton(AF_INET, address, &v4) == 1) max_prefix = 32UL;
    else if (inet_pton(AF_INET6, address, &v6) == 1) max_prefix = 128UL;
    else return 0;

    errno = 0;
    prefix = strtoul(slash + 1, &end, 10);
    return errno == 0 && end != slash + 1 && *end == '\0' && prefix <= max_prefix;
}

CommandResult command_zline(Server *server, Client *client, char *params) {
    char *mask;
    char *reason;
    char resolved_mask[IRC_IP_MAX + 1U];
    BanDb db = {0};
    size_t i = 0U;
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
        snotice_broadcast(server, SNOTICE_BANS, "%s removed ZLINE %s",
                          client->nick, mask);
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
        if (strlen(mask) > IRC_CHANNEL_MASK_MAX ||
            (strchr(mask, '/') != NULL && !valid_cidr_mask(mask))) {
            client_sendf(client, ":%s NOTICE %s :Invalid ZLINE mask: %s",
                         server->config.server_name, client->nick, mask);
            return COMMAND_KEEP_CLIENT;
        }
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
            snotice_broadcast(server, SNOTICE_BANS,
                              "ZLINE matched %s (%s@%s) [real_ip=%s] by %s",
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
        client_sendf(client, ":%s NOTICE %s :ZLINE added: %s (%us, %s)",
                     server->config.server_name, client->nick, mask,
                     server->config.zline_default_duration_seconds, reason);
        snotice_broadcast(server, SNOTICE_BANS,
                          "%s added temporary ZLINE %s for %us (%s)",
                          client->nick, mask,
                          server->config.zline_default_duration_seconds, reason);
    } else {
        client_sendf(client, ":%s NOTICE %s :ZLINE added: %s",
                     server->config.server_name, client->nick, mask);
        snotice_broadcast(server, SNOTICE_BANS, "%s added ZLINE %s (%s)",
                          client->nick, mask, reason);
    }
    return COMMAND_KEEP_CLIENT;
}
