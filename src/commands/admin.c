/**
 * @file admin.c
 * @brief Implementation of IRC server-information commands.
 */

#include "ban_db.h"
#include "commands.h"
#include "config.h"
#include "numerics.h"

#include <stdio.h>
#include <time.h>

typedef struct StatsKlineContext {
    Server *server;
    Client *client;
} StatsKlineContext;

static int stats_kline_row(const BanRecord *record, void *context) {
    StatsKlineContext *stats = context;
    if (record == NULL || stats == NULL || stats->server == NULL || stats->client == NULL)
        return -1;
    client_sendf(stats->client, RPL_STATSKLINE,
                 stats->server->config.server_name, stats->client->nick,
                 record->mask, record->set_by, record->reason);
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
    char version_line[128];
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    client_sendf(client, RPL_INFOSTART,
                 server->config.server_name, client->nick);
    (void)snprintf(version_line, sizeof(version_line),
                   "ScratchIRCd %s on %s", IRCD_VERSION,
                   server->config.server_name);
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
    char selector = params != NULL && params[0] != '\0' ? params[0] : '*';

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
        StatsKlineContext context = {server, client};
        if (client->oper_permissions == 0U) {
            client_sendf(client, ERR_NOPRIVILEGES,
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (ban_db_open(&db, server->config.bans_db) == 0) {
            (void)ban_db_purge_expired(&db);
            (void)ban_db_list(&db, BAN_TYPE_KLINE, stats_kline_row, &context);
            ban_db_close(&db);
        }
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
