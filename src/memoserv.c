/**
 * @file memoserv.c
 * @brief Virtual MemoServ account-to-account messaging service.
 *
 * MemoServ is not a Client, never joins channels, and never appears in normal
 * client lists. Memos are addressed to authenticated NickServ account names
 * and remain in SQLite until the recipient deletes them.
 */

#include "memoserv.h"
#include "memoserv_db.h"
#include "nickserv_db.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void ms_notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":MemoServ!service@%s NOTICE %s :%s",
                 server->config.server_name, client->nick, text);
}

static int require_account(Server *server, Client *client) {
    if (client->account_name[0] != '\0') return 1;
    ms_notice(server, client, "You must identify to NickServ before using MemoServ.");
    return 0;
}

static int parse_id(const char *text, long long *value) {
    char *end = NULL;
    long long parsed;
    if (text == NULL || *text == '\0' || value == NULL) return -1;
    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed <= 0) return -1;
    *value = parsed;
    return 0;
}

static void notify_online_recipient(Server *server, const char *account,
                                    const char *sender, long long memo_id) {
    size_t i;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (server == NULL || account == NULL || sender == NULL) return;
    (void)snprintf(line, sizeof(line),
                   "New memo #%lld from %s. Use /MEMOSERV READ %lld to read it.",
                   memo_id, sender, memo_id);
    for (i = 0U; i < server->client_count; ++i) {
        Client *candidate = server->clients[i];
        if (candidate != NULL && candidate->registered &&
            candidate->account_name[0] != '\0' &&
            strcasecmp(candidate->account_name, account) == 0) {
            ms_notice(server, candidate, line);
        }
    }
}

void memoserv_notify_unread(Server *server, Client *client) {
    MemoServDb db = {0};
    size_t unread = 0U;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (server == NULL || client == NULL || client->account_name[0] == '\0') return;
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) return;
    if (memoserv_db_unread_count(&db, client->account_name, &unread) == 0 && unread != 0U) {
        (void)snprintf(line, sizeof(line),
                       "You have %zu unread memo%s. Use /MEMOSERV LIST to view them.",
                       unread, unread == 1U ? "" : "s");
        ms_notice(server, client, line);
    }
    memoserv_db_close(&db);
}

static void command_send(Server *server, Client *client, char *params) {
    MemoServDb db = {0};
    NickServDb nsdb = {0};
    NickServAccount recipient;
    char *account;
    char *text;
    long long memo_id = 0;
    char line[IRCD_OUTPUT_BUFFER_SIZE];

    if (!require_account(server, client)) return;
    account = params != NULL ? strtok(params, " ") : NULL;
    text = account != NULL ? strtok(NULL, "") : NULL;
    if (text != NULL) {
        while (*text == ' ') ++text;
        if (*text == ':') ++text;
    }
    if (account == NULL || text == NULL || *text == '\0') {
        ms_notice(server, client, "Syntax: SEND <account> :<message>");
        return;
    }
    if (strlen(text) > IRCD_MEMOSERV_TEXT_MAX) {
        ms_notice(server, client, "Memo text is too long.");
        return;
    }
    if (nickserv_db_open(&nsdb, server->config.nickserv_db) != 0 ||
        nickserv_db_get(&nsdb, account, &recipient) != 1 || !recipient.enabled) {
        nickserv_db_close(&nsdb);
        ms_notice(server, client, "That NickServ account does not exist or is disabled.");
        return;
    }
    nickserv_db_close(&nsdb);

    if (memoserv_db_open(&db, server->config.memoserv_db) != 0 ||
        memoserv_db_send(&db, client->account_name, recipient.name, text, &memo_id) != 0) {
        memoserv_db_close(&db);
        ms_notice(server, client, "Memo could not be stored.");
        return;
    }
    memoserv_db_close(&db);
    (void)snprintf(line, sizeof(line), "Memo #%lld sent to %s.", memo_id, recipient.name);
    ms_notice(server, client, line);
    notify_online_recipient(server, recipient.name, client->account_name, memo_id);
}

static void command_status(Server *server, Client *client) {
    MemoServDb db = {0};
    size_t unread = 0U;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0 ||
        memoserv_db_unread_count(&db, client->account_name, &unread) != 0) {
        memoserv_db_close(&db);
        ms_notice(server, client, "Memo database is unavailable.");
        return;
    }
    memoserv_db_close(&db);
    (void)snprintf(line, sizeof(line), "You have %zu unread memo%s.",
                   unread, unread == 1U ? "" : "s");
    ms_notice(server, client, line);
}

static void command_list(Server *server, Client *client) {
    MemoServDb db = {0};
    MemoServMemo memos[IRCD_MEMOSERV_LIST_LIMIT];
    size_t count = 0U;
    size_t i;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0 ||
        memoserv_db_list(&db, client->account_name, memos,
                         IRCD_MEMOSERV_LIST_LIMIT, &count) != 0) {
        memoserv_db_close(&db);
        ms_notice(server, client, "Memo database is unavailable.");
        return;
    }
    memoserv_db_close(&db);
    if (count == 0U) {
        ms_notice(server, client, "You have no memos.");
        return;
    }
    for (i = 0U; i < count; ++i) {
        (void)snprintf(line, sizeof(line), "#%lld %s from %s at %lld",
                       memos[i].id, memos[i].read_at == 0 ? "UNREAD" : "READ",
                       memos[i].sender, memos[i].created_at);
        ms_notice(server, client, line);
    }
}

static void command_read(Server *server, Client *client, char *params) {
    MemoServDb db = {0};
    MemoServMemo memo;
    long long memo_id;
    char *id_text = params != NULL ? strtok(params, " ") : NULL;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    int found;
    if (!require_account(server, client)) return;
    if (parse_id(id_text, &memo_id) != 0) {
        ms_notice(server, client, "Syntax: READ <memo-id>");
        return;
    }
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) {
        ms_notice(server, client, "Memo database is unavailable.");
        return;
    }
    found = memoserv_db_get(&db, client->account_name, memo_id, &memo);
    if (found != 1) {
        memoserv_db_close(&db);
        ms_notice(server, client, found == 0 ? "No such memo." : "Memo lookup failed.");
        return;
    }
    (void)memoserv_db_mark_read(&db, client->account_name, memo_id, (long long)time(NULL));
    memoserv_db_close(&db);
    (void)snprintf(line, sizeof(line), "Memo #%lld from %s at %lld: %s",
                   memo.id, memo.sender, memo.created_at, memo.text);
    ms_notice(server, client, line);
}

static void command_delete(Server *server, Client *client, char *params) {
    MemoServDb db = {0};
    char *what = params != NULL ? strtok(params, " ") : NULL;
    long long memo_id;
    int rc;
    if (!require_account(server, client)) return;
    if (what == NULL) {
        ms_notice(server, client, "Syntax: DEL <memo-id|ALL>");
        return;
    }
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) {
        ms_notice(server, client, "Memo database is unavailable.");
        return;
    }
    if (strcasecmp(what, "ALL") == 0) {
        rc = memoserv_db_delete_all(&db, client->account_name);
        memoserv_db_close(&db);
        ms_notice(server, client, rc == 0 ? "All memos deleted." : "Memo deletion failed.");
        return;
    }
    if (parse_id(what, &memo_id) != 0) {
        memoserv_db_close(&db);
        ms_notice(server, client, "Syntax: DEL <memo-id|ALL>");
        return;
    }
    rc = memoserv_db_delete(&db, client->account_name, memo_id);
    memoserv_db_close(&db);
    ms_notice(server, client, rc == 1 ? "Memo deleted." : rc == 0 ? "No such memo." : "Memo deletion failed.");
}

void memoserv_handle_message(Server *server, Client *client, char *text) {
    char *command;
    char *params;
    if (server == NULL || client == NULL || text == NULL) return;
    command = strtok(text, " ");
    params = strtok(NULL, "");
    if (command == NULL) return;

    if (strcasecmp(command, "SEND") == 0) command_send(server, client, params);
    else if (strcasecmp(command, "LIST") == 0) command_list(server, client);
    else if (strcasecmp(command, "READ") == 0) command_read(server, client, params);
    else if (strcasecmp(command, "DEL") == 0 || strcasecmp(command, "DELETE") == 0)
        command_delete(server, client, params);
    else if (strcasecmp(command, "STATUS") == 0) command_status(server, client);
    else if (strcasecmp(command, "HELP") == 0)
        ms_notice(server, client, "Commands: SEND, LIST, READ, DEL, STATUS, HELP");
    else
        ms_notice(server, client, "Unknown MemoServ command. Use /MEMOSERV HELP.");
}
