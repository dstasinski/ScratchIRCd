/**
 * @file admin.c
 * @brief Implementation of IRC server-information commands.
 */

#include "ban_db.h"
#include "commands.h"
#include "config.h"
#include "geoban_db.h"
#include "numerics.h"

#include <stdio.h>
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

static int stats_ban_row(const BanRecord *record, void *context) {
    StatsBanContext *stats = context;
    if (record == NULL || stats == NULL || stats->server == NULL || stats->client == NULL)
        return -1;

    if (stats->selector == 'k') {
        client_sendf(stats->client, ":%s 216 %s %s %s :%s",
                     stats->server->config.server_name, stats->client->nick,
                     record->mask, record->set_by, record->reason);
    } else {
        client_sendf(stats->client,
                     ":%s 210 %s :ZLINE %s set-by=%s expires=%lld reason=%s",
                     stats->server->config.server_name, stats->client->nick,
                     record->mask, record->set_by, record->expires_at,
                     record->reason);
    }
    return 0;
}

static int stats_geoban_row(const GeoBanRecord *record, void *context) {
    StatsGeoBanContext *stats = context;
    if (record == NULL || stats == NULL || stats->server == NULL || stats->client == NULL)
        return -1;

    client_sendf(stats->client,
                 ":%s 210 %s :GEOBAN %s {%s} set-by=%s expires=%lld reason=%s",
                 stats->server->config.server_name, stats->client->nick,
                 geoban_type_name(record->type), record->value,
                 record->set_by, record->expires_at, record->reason);
    return 0;
}

static int stats_require_oper(Server *server, Client *client) {
    if (client->oper_permissions != 0U) return 1;
    client_sendf(client, ERR_NOPRIVILEGES,
                 server->config.server_name, client->nick);
    return 0;
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
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, RPL_INFOSTART,
                 server->config.server_name, client->nick);
    /* The numeric prefix already identifies the server. Avoid formatting the
     * server name a second time into an arbitrary fixed-size intermediate
     * buffer, which previously allowed a configured long name to be silently
     * truncated. */
    client_sendf(client, RPL_INFO,
                 server->config.server_name, client->nick,
                 "ScratchIRCd " IRCD_VERSION);
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

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, RPL_LINKS,
                 server->config.server_name, client->nick,
                 server->config.server_name, server->config.server_name,
                 0, "ScratchIRCd single-server daemon");
    client_sendf(client, RPL_ENDOFLINKS,
                 server->config.server_name, client->nick, mask);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_stats(Server *server, Client *client, char *params) {
    char selector = params != NULL && params[0] != '\0' ? params[0] : '?';

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
            (void)ban_db_purge_expired(&db);
            (void)ban_db_list(&db, BAN_TYPE_KLINE, stats_ban_row, &context);
            ban_db_close(&db);
        }
        selector = 'k';
    } else if (selector == 'z' || selector == 'Z') {
        BanDb db = {0};
        StatsBanContext context = {server, client, 'z'};
        if (!stats_require_oper(server, client)) return COMMAND_KEEP_CLIENT;
        if (ban_db_open(&db, server->config.bans_db) == 0) {
            (void)ban_db_purge_expired(&db);
            (void)ban_db_list(&db, BAN_TYPE_ZLINE, stats_ban_row, &context);
            ban_db_close(&db);
        }
        selector = 'z';
    } else if (selector == 'g' || selector == 'G') {
        GeoBanDb db = {0};
        StatsGeoBanContext context = {server, client};
        if (!stats_require_oper(server, client)) return COMMAND_KEEP_CLIENT;
        if (geoban_db_open(&db, server->config.bans_db) == 0) {
            (void)geoban_db_list(&db, stats_geoban_row, &context);
            geoban_db_close(&db);
        }
        selector = 'g';
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
                     "STATS g - persistent GeoBAN policies (IRCops only)");
        selector = '?';
    }

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
