/**
 * @file memoserv_admin.c
 * @brief Network-administrator inspection and retention controls for MemoServ.
 */

#include "commands.h"
#include "memoserv_db.h"
#include "message_policy.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int require_netadmin(Server *server, Client *client) {
    if (!client_mode_has(client->modes, CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return 1;
    }
    return 0;
}

static void notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":%s NOTICE %s :%s", server->config.server_name, client->nick, text);
}

CommandResult command_msinfo(Server *server, Client *client, char *params) {
    MemoServDb db = {0};
    char *account;
    size_t total = 0U, unread = 0U;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (command_require_registered(client) || require_netadmin(server, client)) return COMMAND_KEEP_CLIENT;
    account = params != NULL ? strtok(params, " ") : NULL;
    if (account == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name, client->nick, "MSINFO");
        return COMMAND_KEEP_CLIENT;
    }
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0 ||
        memoserv_db_count(&db, account, &total) != 0 ||
        memoserv_db_unread_count(&db, account, &unread) != 0) {
        memoserv_db_close(&db);
        notice(server, client, "MemoServ database is unavailable.");
        return COMMAND_KEEP_CLIENT;
    }
    memoserv_db_close(&db);
    (void)snprintf(line, sizeof(line),
                   "MEMOSERV account=%s stored=%zu unread=%zu quota=%zu retention_days=%u",
                   account, total, unread, server->config.memoserv_quota,
                   server->config.memoserv_retention_days);
    notice(server, client, line);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_mspurge(Server *server, Client *client, char *params) {
    MemoServDb db = {0};
    char *account;
    long long cutoff;
    size_t deleted = 0U;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (command_require_registered(client) || require_netadmin(server, client)) return COMMAND_KEEP_CLIENT;
    account = params != NULL ? strtok(params, " ") : NULL;
    if (account == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name, client->nick, "MSPURGE");
        return COMMAND_KEEP_CLIENT;
    }
    if (server->config.memoserv_retention_days == 0U) {
        notice(server, client, "MemoServ automatic retention is disabled.");
        return COMMAND_KEEP_CLIENT;
    }
    cutoff = (long long)time(NULL) - (long long)server->config.memoserv_retention_days * 86400LL;
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0 ||
        memoserv_db_purge_before(&db, strcmp(account, "*") == 0 ? NULL : account,
                                cutoff, &deleted) != 0) {
        memoserv_db_close(&db);
        notice(server, client, "MemoServ purge failed.");
        return COMMAND_KEEP_CLIENT;
    }
    memoserv_db_close(&db);
    (void)snprintf(line, sizeof(line), "MemoServ purge deleted %zu expired memo%s.",
                   deleted, deleted == 1U ? "" : "s");
    notice(server, client, line);
    snotice_broadcast(server, SNOTICE_SERVICES,
                      "MemoServ purge by %s: target=%s deleted=%zu",
                      client->nick, account, deleted);
    return COMMAND_KEEP_CLIENT;
}
