/**
 * @file memoserv.c
 * @brief Direct /MEMOSERV command wrapper for the virtual MemoServ service.
 */

#include "commands.h"
#include "memoserv.h"
#include "memoserv_db.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static int memo_creating_command(const char *text) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *command;
    if (text == NULL) return 0;
    (void)snprintf(copy, sizeof(copy), "%s", text);
    command = strtok(copy, " ");
    return command != NULL &&
           (strcasecmp(command, "SEND") == 0 ||
            strcasecmp(command, "REPLY") == 0 ||
            strcasecmp(command, "FORWARD") == 0);
}

static int sender_retained_count(Server *server, const char *sender,
                                 size_t *count) {
    MemoServDb db = {0};
    sqlite3_stmt *stmt = NULL;
    int rc = -1;
    if (count != NULL) *count = 0U;
    if (server == NULL || sender == NULL || *sender == '\0' || count == NULL)
        return -1;
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) return -1;
    if (server->config.memoserv_retention_days == 0U) {
        if (sqlite3_prepare_v2(db.handle,
                "SELECT COUNT(*) FROM memos WHERE sender=?1 COLLATE NOCASE",
                -1, &stmt, NULL) != SQLITE_OK) goto done;
        sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_TRANSIENT);
    } else {
        long long cutoff = (long long)time(NULL) -
                           (long long)server->config.memoserv_retention_days * 86400LL;
        if (sqlite3_prepare_v2(db.handle,
                "SELECT COUNT(*) FROM memos WHERE sender=?1 COLLATE NOCASE AND created_at>=?2",
                -1, &stmt, NULL) != SQLITE_OK) goto done;
        sqlite3_bind_text(stmt, 1, sender, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, cutoff);
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        sqlite3_int64 value = sqlite3_column_int64(stmt, 0);
        if (value >= 0) { *count = (size_t)value; rc = 0; }
    }
done:
    if (stmt != NULL) sqlite3_finalize(stmt);
    memoserv_db_close(&db);
    return rc;
}

void command_memoserv_message(Server *server, Client *client, char *text) {
    if (server == NULL || client == NULL || text == NULL) return;
    if (client->account_name[0] != '\0' && memo_creating_command(text) &&
        server->config.memoserv_sender_quota != 0U) {
        size_t sent = 0U;
        if (sender_retained_count(server, client->account_name, &sent) != 0) {
            client_sendf(client, ":MemoServ!service@%s NOTICE %s :Unable to verify your sent-memo quota.",
                         server->config.server_name, client->nick);
            return;
        }
        if (sent >= server->config.memoserv_sender_quota) {
            client_sendf(client, ":MemoServ!service@%s NOTICE %s :You have reached your outstanding sent-memo limit of %zu. Capacity returns when recipients delete memos or retention expires them.",
                         server->config.server_name, client->nick,
                         server->config.memoserv_sender_quota);
            return;
        }
    }
    memoserv_handle_message(server, client, text);
}

CommandResult command_memoserv(Server *server, Client *client, char *params) {
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL || *params == '\0') {
        char help[] = "HELP";
        command_memoserv_message(server, client, help);
    } else {
        command_memoserv_message(server, client, params);
    }
    return COMMAND_KEEP_CLIENT;
}
