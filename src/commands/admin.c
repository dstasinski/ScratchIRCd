/**
 * @file admin.c
 * @brief Implementation of IRC server-information commands.
 */

#include "ban_db.h"
#include "commands.h"
#include "config.h"
#include "geoban_db.h"
#include "nickserv_db.h"
#include "numerics.h"
#include "operator_db.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

typedef struct StatsBanContext {
    Server *server;
    Client *client;
    char selector;
} StatsBanContext;

typedef struct StatsGeoBanContext {
    Server *server;
    Client *client;
} StatsGeoBanContext;

typedef struct StatsElineContext {
    Server *server;
    Client *client;
} StatsElineContext;

static int stats_reason_precision(int base_length, const char *reason) {
    size_t available;
    size_t length;

    if (base_length < 0 || reason == NULL ||
        (size_t)base_length > IRC_LINE_CONTENT_MAX)
        return 0;

    available = IRC_LINE_CONTENT_MAX - (size_t)base_length;
    length = strlen(reason);
    if (length > available) length = available;
    return (int)length;
}

static const char *stats_param_arg(char *params) {
    char *arg;

    if (params == NULL || params[0] == '\0') return NULL;
    arg = params + 1;
    while (*arg == ' ' || *arg == '\t') ++arg;
    if (*arg == ':') {
        ++arg;
        while (*arg == ' ' || *arg == '\t') ++arg;
    }
    return *arg != '\0' ? arg : NULL;
}

static void format_stats_time(long long when, char *buffer, size_t buffer_size) {
    time_t timestamp;
    struct tm utc;
    if (buffer == NULL || buffer_size == 0U) return;
    if (when <= 0) {
        (void)snprintf(buffer, buffer_size, "never");
        return;
    }
    timestamp = (time_t)when;
    if ((long long)timestamp != when || gmtime_r(&timestamp, &utc) == NULL ||
        strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%SZ", &utc) == 0U) {
        (void)snprintf(buffer, buffer_size, "unknown");
    }
}

static char stats_eline_type_letter(BanExceptionType type) {
    switch (type) {
        case BAN_EXCEPTION_KLINE: return 'k';
        case BAN_EXCEPTION_ZLINE: return 'z';
        case BAN_EXCEPTION_CONNECTION_LIMIT: return 'm';
        case BAN_EXCEPTION_DNSBL: return 'B';
        case BAN_EXCEPTION_GEOBAN: return 'G';
    }
    return '?';
}

static int stats_ban_row(const BanRecord *record, void *context) {
    StatsBanContext *stats = context;
    int base_length;
    int reason_precision;

    if (record == NULL || stats == NULL || stats->server == NULL || stats->client == NULL)
        return -1;

    if (stats->selector == 'k') {
        base_length = snprintf(NULL, 0, ":%s 216 %s %s %s :",
                               stats->server->config.server_name,
                               stats->client->nick, record->mask,
                               record->set_by);
        reason_precision = stats_reason_precision(base_length, record->reason);
        client_sendf(stats->client, ":%s 216 %s %s %s :%.*s",
                     stats->server->config.server_name, stats->client->nick,
                     record->mask, record->set_by, reason_precision,
                     record->reason);
    } else {
        base_length = snprintf(NULL, 0,
                               ":%s 210 %s :ZLINE %s set-by=%s expires=%lld reason=",
                               stats->server->config.server_name,
                               stats->client->nick, record->mask,
                               record->set_by, record->expires_at);
        reason_precision = stats_reason_precision(base_length, record->reason);
        client_sendf(stats->client,
                     ":%s 210 %s :ZLINE %s set-by=%s expires=%lld reason=%.*s",
                     stats->server->config.server_name, stats->client->nick,
                     record->mask, record->set_by, record->expires_at,
                     reason_precision, record->reason);
    }
    return stats->client->output_overflowed ? 1 : 0;
}

static int stats_geoban_row(const GeoBanRecord *record, void *context) {
    StatsGeoBanContext *stats = context;
    int base_length;
    int reason_precision;

    if (record == NULL || stats == NULL || stats->server == NULL || stats->client == NULL)
        return -1;

    base_length = snprintf(NULL, 0,
                           ":%s 210 %s :GEOBAN %s {%s} set-by=%s expires=%lld reason=",
                           stats->server->config.server_name,
                           stats->client->nick, geoban_type_name(record->type),
                           record->value, record->set_by, record->expires_at);
    reason_precision = stats_reason_precision(base_length, record->reason);
    client_sendf(stats->client,
                 ":%s 210 %s :GEOBAN %s {%s} set-by=%s expires=%lld reason=%.*s",
                 stats->server->config.server_name, stats->client->nick,
                 geoban_type_name(record->type), record->value,
                 record->set_by, record->expires_at, reason_precision,
                 record->reason);
    return stats->client->output_overflowed ? 1 : 0;
}

static int stats_eline_row(const BanExceptionRecord *record, void *context) {
    StatsElineContext *stats = context;
    char type_letter;
    int base_length;
    int reason_precision;

    if (record == NULL || stats == NULL || stats->server == NULL || stats->client == NULL)
        return -1;

    type_letter = stats_eline_type_letter(record->type);
    base_length = snprintf(NULL, 0,
                           ":%s 210 %s :ELINE %c %s set-by=%s expires=%lld reason=",
                           stats->server->config.server_name,
                           stats->client->nick, type_letter, record->mask,
                           record->set_by, record->expires_at);
    reason_precision = stats_reason_precision(base_length, record->reason);
    client_sendf(stats->client,
                 ":%s 210 %s :ELINE %c %s set-by=%s expires=%lld reason=%.*s",
                 stats->server->config.server_name, stats->client->nick,
                 type_letter, record->mask, record->set_by, record->expires_at,
                 reason_precision, record->reason);
    return stats->client->output_overflowed ? 1 : 0;
}

static int stats_require_oper(Server *server, Client *client) {
    if (client->oper_permissions != 0U) return 1;
    client_sendf(client, ERR_NOPRIVILEGES,
                 server->config.server_name, client->nick);
    return 0;
}

static void stats_nickserv_account(Server *server, Client *client,
                                   const char *account_name) {
    NickServDb db = {0};
    NickServAccount account;
    char created[32];
    char updated[32];
    char identified[32];
    int found;

    if (account_name == NULL) {
        client_sendf(client, ":%s 210 %s :NICKSERV syntax: STATS N <account>",
                     server->config.server_name, client->nick);
        return;
    }
    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) {
        client_sendf(client, ":%s 210 %s :NICKSERV account database unavailable",
                     server->config.server_name, client->nick);
        return;
    }
    found = nickserv_db_get(&db, account_name, &account);
    nickserv_db_close(&db);
    if (found != 1) {
        client_sendf(client, ":%s 210 %s :NICKSERV account=%s not-found",
                     server->config.server_name, client->nick, account_name);
        return;
    }
    format_stats_time(account.created_at, created, sizeof(created));
    format_stats_time(account.updated_at, updated, sizeof(updated));
    format_stats_time(account.last_identified_at, identified, sizeof(identified));
    client_sendf(client,
                 ":%s 210 %s :NICKSERV account=%s enabled=%d created=%s updated=%s last_identified=%s",
                 server->config.server_name, client->nick, account_name,
                 account.enabled ? 1 : 0, created, updated, identified);
}

static void stats_operator_account(Server *server, Client *client,
                                   const char *oper_name) {
    OperatorDb db = {0};
    OperatorRecord record;
    char created[32];
    char updated[32];
    char opered[32];
    int found;

    if (oper_name == NULL) {
        client_sendf(client, ":%s 210 %s :OPER syntax: STATS O <oper>",
                     server->config.server_name, client->nick);
        return;
    }
    if (operator_db_open(&db, server->config.operators_db) != 0) {
        client_sendf(client, ":%s 210 %s :OPER operator database unavailable",
                     server->config.server_name, client->nick);
        return;
    }
    found = operator_db_get(&db, oper_name, &record);
    operator_db_close(&db);
    if (found != 1) {
        client_sendf(client, ":%s 210 %s :OPER name=%s not-found",
                     server->config.server_name, client->nick, oper_name);
        return;
    }
    format_stats_time(record.created_at, created, sizeof(created));
    format_stats_time(record.updated_at, updated, sizeof(updated));
    format_stats_time(record.last_opered_at, opered, sizeof(opered));
    client_sendf(client,
                 ":%s 210 %s :OPER name=%s enabled=%d created=%s updated=%s last_opered=%s",
                 server->config.server_name, client->nick, record.name,
                 record.enabled ? 1 : 0, created, updated, opered);
}

CommandResult command_admin(Server *server, Client *client, char *params) {
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    if (server->config.admin_location1[0] == '\0' &&
        server->config.admin_location2[0] == '\0' &&
        server->config.admin_email[0] == '\0') {
        client_sendf(client, ERR_NOADMININFO,
                     server->config.server_name, client->nick,
                     server->config.server_name);
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, RPL_ADMINME,
                 server->config.server_name, client->nick,
                 server->config.server_name);
    client_sendf(client, RPL_ADMINLOC1,
                 server->config.server_name, client->nick,
                 server->config.admin_location1);
    client_sendf(client, RPL_ADMINLOC2,
                 server->config.server_name, client->nick,
                 server->config.admin_location2);
    client_sendf(client, RPL_ADMINEMAIL,
                 server->config.server_name, client->nick,
                 server->config.admin_email);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_info(Server *server, Client *client, char *params) {
    char version_line[IRC_LINE_CONTENT_MAX + 1U];
    size_t overhead;
    size_t repeated_name_limit;
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, RPL_INFOSTART,
                 server->config.server_name, client->nick);

    /* RPL_INFO is ":server 371 nick :text". The server name is intentionally
     * repeated in the traditional ScratchIRCd version text, but bound that
     * second occurrence by the actual remaining IRC wire budget rather than
     * by an unrelated 128-byte intermediate buffer. */
    overhead = 1U + strlen(server->config.server_name) + strlen(" 371 ") +
               strlen(client->nick) + strlen(" :") + strlen("ScratchIRCd ") +
               strlen(IRCD_VERSION) + strlen(" on ");
    repeated_name_limit = overhead < IRC_LINE_CONTENT_MAX
                            ? IRC_LINE_CONTENT_MAX - overhead : 0U;
    if (repeated_name_limit > strlen(server->config.server_name))
        repeated_name_limit = strlen(server->config.server_name);
    (void)snprintf(version_line, sizeof(version_line),
                   "ScratchIRCd %s on %.*s", IRCD_VERSION,
                   (int)repeated_name_limit, server->config.server_name);
    client_sendf(client, RPL_INFO,
                 server->config.server_name, client->nick, version_line);
    client_sendf(client, RPL_INFO,
                 server->config.server_name, client->nick,
                 "Single-server IRC daemon written in C for Linux");
    client_sendf(client, RPL_INFO,
                 server->config.server_name, client->nick,
                 "NickServ, ChanServ, and MemoServ are virtual services");
    client_sendf(client, RPL_ENDOFINFO,
                 server->config.server_name, client->nick);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_links(Server *server, Client *client, char *params) {
    const char *mask = params != NULL && params[0] != '\0' ? params : "*";
    int base_length;
    size_t mask_limit;
    size_t mask_length;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, RPL_LINKS,
                 server->config.server_name, client->nick,
                 server->config.server_name, server->config.server_name,
                 0, "ScratchIRCd single-server daemon");

    base_length = snprintf(NULL, 0, ":%s 365 %s  :End of /LINKS list.",
                           server->config.server_name, client->nick);
    if (base_length < 0 || (size_t)base_length > IRC_LINE_CONTENT_MAX)
        return COMMAND_KEEP_CLIENT;
    mask_limit = IRC_LINE_CONTENT_MAX - (size_t)base_length;
    mask_length = strlen(mask);
    if (mask_length > mask_limit) mask_length = mask_limit;
    client_sendf(client, ":%s 365 %s %.*s :End of /LINKS list.",
                 server->config.server_name, client->nick,
                 (int)mask_length, mask);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_stats(Server *server, Client *client, char *params) {
    char selector = params != NULL && params[0] != '\0' ? params[0] : '?';
    const char *arg = stats_param_arg(params);

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    if (selector == 'u' || selector == 'U') {
        time_t now = time(NULL);
        long uptime = server->started_at > 0 && now > server->started_at
                          ? (long)(now - server->started_at) : 0L;
        long days = uptime / 86400L;
        long hours = (uptime % 86400L) / 3600L;
        long minutes = (uptime % 3600L) / 60L;
        long seconds = uptime % 60L;
        client_sendf(client, RPL_STATSUPTIME,
                     server->config.server_name, client->nick,
                     days, hours, minutes, seconds);
    } else if (selector == 'k' || selector == 'K') {
        BanDb db = {0};
        StatsBanContext context = {server, client, 'k'};
        if (!stats_require_oper(server, client)) return COMMAND_KEEP_CLIENT;
        if (ban_db_open(&db, server->config.bans_db) == 0) {
            (void)ban_db_list(&db, BAN_TYPE_KLINE, stats_ban_row, &context);
            ban_db_close(&db);
        }
        selector = 'k';
    } else if (selector == 'z' || selector == 'Z') {
        BanDb db = {0};
        StatsBanContext context = {server, client, 'z'};
        if (!stats_require_oper(server, client)) return COMMAND_KEEP_CLIENT;
        if (ban_db_open(&db, server->config.bans_db) == 0) {
            (void)ban_db_list(&db, BAN_TYPE_ZLINE, stats_ban_row, &context);
            ban_db_close(&db);
        }
        selector = 'z';
    } else if (selector == 'e' || selector == 'E') {
        BanDb db = {0};
        StatsElineContext context = {server, client};
        if (!stats_require_oper(server, client)) return COMMAND_KEEP_CLIENT;
        if (ban_db_open(&db, server->config.bans_db) == 0) {
            (void)ban_exception_db_list_all(&db, stats_eline_row, &context);
            ban_db_close(&db);
        }
        selector = 'e';
    } else if (selector == 'g' || selector == 'G') {
        GeoBanDb db = {0};
        StatsGeoBanContext context = {server, client};
        if (!stats_require_oper(server, client)) return COMMAND_KEEP_CLIENT;
        if (geoban_db_open(&db, server->config.bans_db) == 0) {
            (void)geoban_db_list(&db, stats_geoban_row, &context);
            geoban_db_close(&db);
        }
        selector = 'g';
    } else if (selector == 'n' || selector == 'N') {
        if (!stats_require_oper(server, client)) return COMMAND_KEEP_CLIENT;
        stats_nickserv_account(server, client, arg);
        selector = 'N';
    } else if (selector == 'o' || selector == 'O') {
        if (!stats_require_oper(server, client)) return COMMAND_KEEP_CLIENT;
        stats_operator_account(server, client, arg);
        selector = 'O';
    } else if (selector == '?' || selector == 'h' || selector == 'H') {
        client_sendf(client, RPL_STATSHELP,
                     server->config.server_name, client->nick,
                     "STATS u - server uptime");
        client_sendf(client, RPL_STATSHELP,
                     server->config.server_name, client->nick,
                     "STATS k - persistent KLINEs (IRCops only)");
        client_sendf(client, RPL_STATSHELP,
                     server->config.server_name, client->nick,
                     "STATS z - persistent ZLINEs (IRCops only)");
        client_sendf(client, RPL_STATSHELP,
                     server->config.server_name, client->nick,
                     "STATS e - persistent E-LINE exceptions (IRCops only)");
        client_sendf(client, RPL_STATSHELP,
                     server->config.server_name, client->nick,
                     "STATS g - persistent GeoBAN policies (IRCops only)");
        client_sendf(client, RPL_STATSHELP,
                     server->config.server_name, client->nick,
                     "STATS N <account> - NickServ account last successful identify (IRCops only)");
        client_sendf(client, RPL_STATSHELP,
                     server->config.server_name, client->nick,
                     "STATS O <oper> - operator account last successful OPER (IRCops only)");
        selector = '?';
    }

    if (!client->output_overflowed)
        client_sendf(client, RPL_ENDOFSTATS,
                     server->config.server_name, client->nick, selector);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_time(Server *server, Client *client, char *params) {
    time_t now;
    struct tm local;
    char text[128];
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    now = time(NULL);
    if (localtime_r(&now, &local) == NULL ||
        strftime(text, sizeof(text), "%a %b %d %Y -- %H:%M:%S %Z", &local) == 0U) {
        text[0] = '\0';
    }
    client_sendf(client, RPL_TIME,
                 server->config.server_name, client->nick,
                 server->config.server_name,
                 text[0] != '\0' ? text : "Unknown server time");
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_version(Server *server, Client *client, char *params) {
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, ":%s 351 %s %s %s :single-server C11 Linux",
                 server->config.server_name, client->nick,
                 IRCD_VERSION, server->config.server_name);
    return COMMAND_KEEP_CLIENT;
}
