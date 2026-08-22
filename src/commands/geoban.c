/**
 * @file geoban.c
 * @brief Operator-managed persistent GeoIP policy bans.
 */

#include "commands.h"
#include "geoban_db.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct GeoBanListContext {
    Server *server;
    Client *client;
    size_t count;
} GeoBanListContext;

static char *skip_space(char *p) {
    while (p != NULL && (*p == ' ' || *p == '\t')) ++p;
    return p;
}

/**
 * Consume one IRC parameter, with Tcl-style braces accepted for values that
 * contain spaces. The braces are grouping syntax and are not stored.
 */
static char *next_field(char **cursor, char *output, size_t output_size) {
    char *p, *start;
    size_t used = 0U;
    int braced = 0;
    if (cursor == NULL || *cursor == NULL || output == NULL || output_size == 0U) return NULL;
    p = skip_space(*cursor);
    if (*p == '\0') return NULL;
    if (*p == '{') {
        braced = 1;
        ++p;
        start = p;
        while (*p != '\0' && *p != '}') ++p;
        if (*p != '}') return NULL;
        while (start < p && used + 1U < output_size) output[used++] = *start++;
        output[used] = '\0';
        ++p;
        if (*p != '\0' && *p != ' ' && *p != '\t') return NULL;
    } else {
        start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') ++p;
        while (start < p && used + 1U < output_size) output[used++] = *start++;
        output[used] = '\0';
    }
    if (used == 0U && !braced) return NULL;
    *cursor = p;
    return output;
}

static int list_callback(const GeoBanRecord *record, void *context) {
    GeoBanListContext *ctx = context;
    char expiry[64];
    if (record->expires_at == 0) {
        (void)snprintf(expiry, sizeof(expiry), "permanent");
    } else {
        long long remaining = record->expires_at - (long long)time(NULL);
        if (remaining < 0) remaining = 0;
        (void)snprintf(expiry, sizeof(expiry), "%llds", remaining);
    }
    client_sendf(ctx->client,
                 ":%s NOTICE %s :GEOBAN %s {%s} expires=%s set_by=%s :%s",
                 ctx->server->config.server_name, ctx->client->nick,
                 geoban_type_name(record->type), record->value, expiry,
                 record->set_by[0] != '\0' ? record->set_by : "unknown",
                 record->reason[0] != '\0' ? record->reason : "GeoIP policy ban");
    ++ctx->count;
    return 0;
}

static int require_geoban(Client *client, Server *server) {
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_GEOBAN)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return 0;
    }
    return 1;
}

static void disconnect_matches(Server *server, Client *setter,
                               GeoBanType type, const char *value,
                               const char *reason) {
    GeoBanDb db = {0};
    size_t i = 0U;
    (void)type;
    (void)value;
    if (geoban_db_open(&db, server->config.bans_db) != 0) return;
    while (i < server->client_count) {
        Client *target = server->clients[i];
        GeoBanRecord match;
        if (target != setter && target->registered && target->geoip_complete &&
            geoban_db_match(&db, &target->geoip, &match) == 1) {
            client_sendf(target, ERR_YOUREBANNEDCREEP,
                         server->config.server_name, command_reply_nick(target),
                         server->config.admin_email);
            server_disconnect(server, target,
                              reason != NULL && *reason != '\0' ? reason : "GeoIP policy ban");
            continue;
        }
        ++i;
    }
    geoban_db_close(&db);
}

CommandResult command_geoban(Server *server, Client *client, char *params) {
    GeoBanDb db = {0};
    GeoBanType type;
    char type_text[32];
    char value_raw[IRCD_GEOIP_ORG_MAX + 1U];
    char value[IRCD_GEOIP_ORG_MAX + 1U];
    char duration_text[32];
    unsigned int duration;
    char *cursor = params;
    char *reason;
    char notice[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!require_geoban(client, server)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "GEOBAN");
        return COMMAND_KEEP_CLIENT;
    }

    if (next_field(&cursor, type_text, sizeof(type_text)) == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "GEOBAN");
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(type_text, "LIST") == 0) {
        GeoBanListContext ctx = {server, client, 0U};
        if (geoban_db_open(&db, server->config.bans_db) != 0 ||
            geoban_db_list(&db, list_callback, &ctx) != 0) {
            geoban_db_close(&db);
            client_sendf(client, ":%s NOTICE %s :GEOBAN LIST failed",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        geoban_db_close(&db);
        client_sendf(client, ":%s NOTICE %s :End of GEOBAN list (%zu active)",
                     server->config.server_name, client->nick, ctx.count);
        return COMMAND_KEEP_CLIENT;
    }

    if (geoban_type_parse(type_text, &type) != 0 ||
        next_field(&cursor, value_raw, sizeof(value_raw)) == NULL ||
        next_field(&cursor, duration_text, sizeof(duration_text)) == NULL ||
        geoban_normalize_value(type, value_raw, value, sizeof(value)) != 0 ||
        geoban_duration_parse(duration_text, &duration) != 0) {
        client_sendf(client, ":%s NOTICE %s :Invalid GEOBAN syntax or value",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    reason = skip_space(cursor);
    if (reason != NULL && *reason == ':') ++reason;
    if (reason == NULL || *reason == '\0') reason = "GeoIP policy ban";

    if (geoban_db_open(&db, server->config.bans_db) != 0 ||
        geoban_db_add(&db, type, value, reason,
                      client->oper_name[0] != '\0' ? client->oper_name : client->nick,
                      duration) != 0) {
        geoban_db_close(&db);
        client_sendf(client, ":%s NOTICE %s :GEOBAN add failed",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    geoban_db_close(&db);

    client_sendf(client, ":%s NOTICE %s :GEOBAN added: %s {%s} %s",
                 server->config.server_name, client->nick,
                 geoban_type_name(type), value,
                 duration == 0U ? "permanent" : duration_text);
    (void)snprintf(notice, sizeof(notice), "%s added GEOBAN %s {%s} (%s)",
                   client->nick, geoban_type_name(type), value, reason);
    server_notice_broadcast(server, notice);
    disconnect_matches(server, client, type, value, reason);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_ungeoban(Server *server, Client *client, char *params) {
    GeoBanDb db = {0};
    GeoBanType type;
    char type_text[32];
    char value_raw[IRCD_GEOIP_ORG_MAX + 1U];
    char value[IRCD_GEOIP_ORG_MAX + 1U];
    char *cursor = params;
    char notice[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!require_geoban(client, server)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || next_field(&cursor, type_text, sizeof(type_text)) == NULL ||
        geoban_type_parse(type_text, &type) != 0 ||
        next_field(&cursor, value_raw, sizeof(value_raw)) == NULL ||
        geoban_normalize_value(type, value_raw, value, sizeof(value)) != 0) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "UNGEOBAN");
        return COMMAND_KEEP_CLIENT;
    }

    if (geoban_db_open(&db, server->config.bans_db) != 0 ||
        geoban_db_delete(&db, type, value) != 0) {
        geoban_db_close(&db);
        client_sendf(client, ":%s NOTICE %s :UNGEOBAN failed: %s {%s}",
                     server->config.server_name, client->nick,
                     geoban_type_name(type), value);
        return COMMAND_KEEP_CLIENT;
    }
    geoban_db_close(&db);
    client_sendf(client, ":%s NOTICE %s :GEOBAN removed: %s {%s}",
                 server->config.server_name, client->nick,
                 geoban_type_name(type), value);
    (void)snprintf(notice, sizeof(notice), "%s removed GEOBAN %s {%s}",
                   client->nick, geoban_type_name(type), value);
    server_notice_broadcast(server, notice);
    return COMMAND_KEEP_CLIENT;
}
