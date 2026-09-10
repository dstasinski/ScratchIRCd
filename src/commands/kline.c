/**
 * @file kline.c
 * @brief Persistent user@host KLINE and E-LINE management.
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

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct KlineDisconnectContext {
    Server *server;
    Client *setter;
    const char *reason;
    const char *added_mask;
} KlineDisconnectContext;

typedef struct ElineListContext {
    Server *server;
    Client *client;
    unsigned int count;
} ElineListContext;

static int kline_disconnect_row(const BanRecord *record, void *context) {
    KlineDisconnectContext *ctx = context;
    size_t i = 0U;
    if (record == NULL || ctx == NULL || ctx->server == NULL) return -1;
    while (i < ctx->server->client_count) {
        Client *target = ctx->server->clients[i];
        char host_identity[IRCD_MESSAGE_BUFFER_SIZE];
        char ip_identity[IRCD_MESSAGE_BUFFER_SIZE];
        const char *first;

        (void)snprintf(ip_identity, sizeof(ip_identity), "%s@%s",
                       target->user, target->real_ip);
        if (target->real_host[0] != '\0') {
            (void)snprintf(host_identity, sizeof(host_identity), "%s@%s",
                           target->user, target->real_host);
            first = host_identity;
        } else {
            first = ip_identity;
        }

        if (target != ctx->setter && ban_record_matches(record, first, ip_identity)) {
            snotice_broadcast(ctx->server, SNOTICE_BANS,
                              "KLINE matched %s (%s@%s) [real_ip=%s] by %s",
                              command_reply_nick(target), target->user,
                              target->display_host, target->real_ip,
                              ctx->added_mask != NULL ? ctx->added_mask : record->mask);
            client_sendf(target, ERR_YOUREBANNEDCREEP,
                         ctx->server->config.server_name,
                         command_reply_nick(target), ctx->server->config.admin_email);
            server_disconnect(ctx->server, target,
                              ctx->reason != NULL && *ctx->reason != '\0'
                                  ? ctx->reason : record->reason);
            continue;
        }
        ++i;
    }
    return 0;
}

static char eline_type_letter(BanExceptionType type) {
    switch (type) {
        case BAN_EXCEPTION_KLINE: return 'k';
        case BAN_EXCEPTION_ZLINE: return 'z';
        case BAN_EXCEPTION_CONNECTION_LIMIT: return 'm';
        case BAN_EXCEPTION_DNSBL: return 'B';
        case BAN_EXCEPTION_GEOBAN: return 'G';
        default: return '?';
    }
}

static BanExceptionType eline_type_from_letter(char letter) {
    switch (letter) {
        case 'k': return BAN_EXCEPTION_KLINE;
        case 'z': return BAN_EXCEPTION_ZLINE;
        case 'm': return BAN_EXCEPTION_CONNECTION_LIMIT;
        case 'B': return BAN_EXCEPTION_DNSBL;
        case 'G': return BAN_EXCEPTION_GEOBAN;
        default: return (BanExceptionType)0;
    }
}

static int parse_eline_type_filter(const char *text, BanExceptionType *type) {
    if (text == NULL || type == NULL || text[0] == '\0' || text[1] != '\0') return -1;
    *type = eline_type_from_letter(text[0]);
    return *type != (BanExceptionType)0 ? 0 : -1;
}

static int eline_type_selected(const BanExceptionType *types, size_t count,
                               BanExceptionType type) {
    size_t i;
    for (i = 0U; i < count; ++i)
        if (types[i] == type) return 1;
    return 0;
}

static int parse_eline_types(const char *text, BanExceptionType *types,
                             size_t size, size_t *count) {
    const char *p;
    if (text == NULL || types == NULL || count == NULL || *text == '\0') return -1;
    *count = 0U;
    for (p = text; *p != '\0'; ++p) {
        BanExceptionType type = eline_type_from_letter(*p);
        if (type == (BanExceptionType)0) return -1;
        if (eline_type_selected(types, *count, type)) continue;
        if (*count >= size) return -1;
        types[(*count)++] = type;
    }
    return *count > 0U ? 0 : -1;
}

static int parse_eline_duration(const char *text, unsigned int *seconds) {
    char *end = NULL;
    unsigned long number;
    unsigned long multiplier = 1UL;

    if (text == NULL || seconds == NULL || *text == '\0') return -1;
    if (strcmp(text, "0") == 0) {
        *seconds = 0U;
        return 0;
    }

    errno = 0;
    number = strtoul(text, &end, 10);
    if (errno != 0 || end == text || number == 0UL || end[0] == '\0' || end[1] != '\0')
        return -1;

    switch (*end) {
        case 'm': multiplier = 60UL; break;
        case 'h': multiplier = 60UL * 60UL; break;
        case 'd': multiplier = 24UL * 60UL * 60UL; break;
        case 'w': multiplier = 7UL * 24UL * 60UL * 60UL; break;
        default: return -1;
    }
    if (number > (unsigned long)UINT_MAX / multiplier) return -1;
    *seconds = (unsigned int)(number * multiplier);
    return 0;
}

static int valid_eline_cidr_host(const char *host) {
    char address[IRC_IP_MAX + 1U];
    const char *slash;
    char *end = NULL;
    unsigned long prefix;
    struct in_addr v4;
    struct in6_addr v6;
    unsigned long max_prefix;

    if (host == NULL || (slash = strchr(host, '/')) == NULL || slash == host ||
        strchr(slash + 1, '/') != NULL || (size_t)(slash - host) >= sizeof(address))
        return 0;
    memcpy(address, host, (size_t)(slash - host));
    address[slash - host] = '\0';

    if (inet_pton(AF_INET, address, &v4) == 1) max_prefix = 32UL;
    else if (inet_pton(AF_INET6, address, &v6) == 1) max_prefix = 128UL;
    else return 0;

    errno = 0;
    prefix = strtoul(slash + 1, &end, 10);
    return errno == 0 && end != slash + 1 && *end == '\0' && prefix <= max_prefix;
}

static int valid_eline_mask(const char *mask) {
    const char *host;
    if (mask == NULL || *mask == '\0' || strlen(mask) > IRC_CHANNEL_MASK_MAX ||
        strchr(mask, '\r') != NULL || strchr(mask, '\n') != NULL)
        return 0;
    host = strchr(mask, '@');
    host = host != NULL ? host + 1 : mask;
    if (strchr(host, '/') != NULL && !valid_eline_cidr_host(host)) return 0;
    return 1;
}

static int eline_list_row(const BanExceptionRecord *record, void *context) {
    ElineListContext *ctx = context;
    char expires[32];
    if (record == NULL || ctx == NULL || ctx->server == NULL || ctx->client == NULL) return -1;
    if (record->expires_at == 0)
        (void)snprintf(expires, sizeof(expires), "permanent");
    else
        (void)snprintf(expires, sizeof(expires), "%lld", record->expires_at);
    client_sendf(ctx->client,
                 ":%s NOTICE %s :ELINE %c %s set_by=%s created=%lld expires=%s :%s",
                 ctx->server->config.server_name, ctx->client->nick,
                 eline_type_letter(record->type), record->mask,
                 record->set_by, record->created_at, expires, record->reason);
    ++ctx->count;
    return 0;
}

CommandResult command_eline(Server *server, Client *client, char *params) {
    char *first;
    BanDb db = {0};

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "ELINE");
        return COMMAND_KEEP_CLIENT;
    }

    first = strtok(params, " ");
    if (first == NULL || *first == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "ELINE");
        return COMMAND_KEEP_CLIENT;
    }

    if (strcasecmp(first, "LIST") == 0) {
        char *filter = strtok(NULL, " ");
        ElineListContext context = {server, client, 0U};
        int rc;
        if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
            client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (ban_db_open(&db, server->config.bans_db) != 0) {
            client_sendf(client, ":%s NOTICE %s :ELINE list failed",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (filter != NULL) {
            BanExceptionType type;
            if (parse_eline_type_filter(filter, &type) != 0 || strtok(NULL, " ") != NULL) {
                ban_db_close(&db);
                client_sendf(client, ":%s NOTICE %s :Invalid ELINE type filter",
                             server->config.server_name, client->nick);
                return COMMAND_KEEP_CLIENT;
            }
            rc = ban_exception_db_list(&db, type, eline_list_row, &context);
        } else {
            rc = ban_exception_db_list_all(&db, eline_list_row, &context);
        }
        ban_db_close(&db);
        if (rc != 0) {
            client_sendf(client, ":%s NOTICE %s :ELINE list failed",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        client_sendf(client, ":%s NOTICE %s :End of ELINE list (%u entries)",
                     server->config.server_name, client->nick, context.count);
        return COMMAND_KEEP_CLIENT;
    }

    if (*first == '-') {
        const char *mask = first + 1;
        if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_ELINE)) {
            client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (*mask == '\0' || ban_db_open(&db, server->config.bans_db) != 0 ||
            ban_exception_db_delete_mask(&db, mask) != 0) {
            ban_db_close(&db);
            client_sendf(client, ":%s NOTICE %s :ELINE removal failed",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        ban_db_close(&db);
        client_sendf(client, ":%s NOTICE %s :ELINE removed: %s",
                     server->config.server_name, client->nick, mask);
        snotice_broadcast(server, SNOTICE_BANS, "%s removed ELINE %s",
                          client->nick, mask);
        return COMMAND_KEEP_CLIENT;
    }

    {
        char *types_text = strtok(NULL, " ");
        char *duration_text = strtok(NULL, " ");
        char *reason = strtok(NULL, "");
        BanExceptionType types[5];
        size_t type_count = 0U;
        size_t i;
        unsigned int duration_seconds;
        const char *setter = client->oper_name[0] != '\0' ? client->oper_name : client->nick;

        if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_ELINE)) {
            client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (types_text == NULL || duration_text == NULL || reason == NULL || *reason == '\0') {
            client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                         client->nick, "ELINE");
            return COMMAND_KEEP_CLIENT;
        }
        if (*reason == ':') ++reason;
        if (*reason == '\0') {
            client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                         client->nick, "ELINE");
            return COMMAND_KEEP_CLIENT;
        }
        if (!valid_eline_mask(first) || parse_eline_types(types_text, types, 5U, &type_count) != 0 ||
            parse_eline_duration(duration_text, &duration_seconds) != 0) {
            client_sendf(client, ":%s NOTICE %s :Invalid ELINE syntax",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (ban_db_open(&db, server->config.bans_db) != 0) {
            client_sendf(client, ":%s NOTICE %s :ELINE failed",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        for (i = 0U; i < type_count; ++i) {
            int rc = duration_seconds == 0U
                         ? ban_exception_db_add(&db, types[i], first, reason, setter)
                         : ban_exception_db_add_timed(&db, types[i], first, reason, setter,
                                                      duration_seconds);
            if (rc != 0) {
                ban_db_close(&db);
                client_sendf(client, ":%s NOTICE %s :ELINE failed",
                             server->config.server_name, client->nick);
                return COMMAND_KEEP_CLIENT;
            }
        }
        ban_db_close(&db);
        client_sendf(client, ":%s NOTICE %s :ELINE added: %s %s",
                     server->config.server_name, client->nick, first, types_text);
        snotice_broadcast(server, SNOTICE_BANS, "%s added ELINE %s %s (%s)",
                          client->nick, first, types_text, reason);
        return COMMAND_KEEP_CLIENT;
    }
}

CommandResult command_kline(Server *server, Client *client, char *params) {
    char *mask;
    char *reason;
    char resolved_mask[IRC_CHANNEL_MASK_MAX + 1U];
    BanDb db = {0};
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

    {
        KlineDisconnectContext context = {server, client, reason, mask};
        (void)ban_db_list(&db, BAN_TYPE_KLINE, kline_disconnect_row, &context);
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
