/**
 * @file memoserv.c
 * @brief Virtual MemoServ account-to-account messaging service.
 *
 * MemoServ is not a Client, never joins channels, and never appears in normal
 * client lists. Memos are addressed to authenticated NickServ account names.
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

static void purge_expired(Server *server, MemoServDb *db) {
    long long cutoff;
    if (server == NULL || db == NULL || server->config.memoserv_retention_days == 0U) return;
    cutoff = (long long)time(NULL) - (long long)server->config.memoserv_retention_days * 86400LL;
    (void)memoserv_db_purge_before(db, NULL, cutoff, NULL);
}

static int load_enabled_account(Server *server, const char *name, NickServAccount *account) {
    NickServDb db = {0};
    int found;
    if (server == NULL || name == NULL || account == NULL) return 0;
    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) return 0;
    found = nickserv_db_get(&db, name, account);
    nickserv_db_close(&db);
    return found == 1 && account->enabled;
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
            strcasecmp(candidate->account_name, account) == 0)
            ms_notice(server, candidate, line);
    }
}

void memoserv_notify_unread(Server *server, Client *client) {
    MemoServDb db = {0};
    size_t unread = 0U;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (server == NULL || client == NULL || client->account_name[0] == '\0') return;
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) return;
    purge_expired(server, &db);
    if (memoserv_db_unread_count(&db, client->account_name, &unread) == 0 && unread != 0U) {
        (void)snprintf(line, sizeof(line),
                       "You have %zu unread memo%s. Use /MEMOSERV LIST to view them.",
                       unread, unread == 1U ? "" : "s");
        ms_notice(server, client, line);
    }
    memoserv_db_close(&db);
}

static int store_memo(Server *server, Client *client, const char *recipient_name,
                      const char *text, long long *memo_id) {
    MemoServDb db = {0};
    NickServAccount recipient;
    size_t count = 0U;
    if (!load_enabled_account(server, recipient_name, &recipient)) {
        ms_notice(server, client, "That NickServ account does not exist or is disabled.");
        return 0;
    }
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) {
        ms_notice(server, client, "Memo database is unavailable.");
        return 0;
    }
    purge_expired(server, &db);
    if (memoserv_db_count(&db, recipient.name, &count) != 0) {
        memoserv_db_close(&db);
        ms_notice(server, client, "Memo database is unavailable.");
        return 0;
    }
    if (count >= server->config.memoserv_quota) {
        memoserv_db_close(&db);
        ms_notice(server, client, "Recipient memo box is full.");
        return 0;
    }
    if (memoserv_db_send(&db, client->account_name, recipient.name, text, memo_id) != 0) {
        memoserv_db_close(&db);
        ms_notice(server, client, "Memo could not be stored.");
        return 0;
    }
    memoserv_db_close(&db);
    notify_online_recipient(server, recipient.name, client->account_name, *memo_id);
    return 1;
}

static void command_send(Server *server, Client *client, char *params) {
    char *account;
    char *text;
    long long memo_id = 0;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    account = params != NULL ? strtok(params, " ") : NULL;
    text = account != NULL ? strtok(NULL, "") : NULL;
    if (text != NULL) { while (*text == ' ') ++text; if (*text == ':') ++text; }
    if (account == NULL || text == NULL || *text == '\0') {
        ms_notice(server, client, "Syntax: SEND <account> :<message>"); return;
    }
    if (strlen(text) > IRCD_MEMOSERV_TEXT_MAX) {
        ms_notice(server, client, "Memo text is too long."); return;
    }
    if (store_memo(server, client, account, text, &memo_id)) {
        (void)snprintf(line, sizeof(line), "Memo #%lld sent to %s.", memo_id, account);
        ms_notice(server, client, line);
    }
}

static void command_status(Server *server, Client *client) {
    MemoServDb db = {0};
    size_t unread = 0U, total = 0U;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) {
        ms_notice(server, client, "Memo database is unavailable."); return;
    }
    purge_expired(server, &db);
    if (memoserv_db_unread_count(&db, client->account_name, &unread) != 0 ||
        memoserv_db_count(&db, client->account_name, &total) != 0) {
        memoserv_db_close(&db); ms_notice(server, client, "Memo database is unavailable."); return;
    }
    memoserv_db_close(&db);
    (void)snprintf(line, sizeof(line), "Memos: %zu/%zu stored, %zu unread.",
                   total, server->config.memoserv_quota, unread);
    ms_notice(server, client, line);
}

static void list_records(Server *server, Client *client, int sent) {
    MemoServDb db = {0};
    MemoServMemo memos[IRCD_MEMOSERV_LIST_LIMIT];
    size_t count = 0U, i;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) {
        ms_notice(server, client, "Memo database is unavailable."); return;
    }
    purge_expired(server, &db);
    if ((sent ? memoserv_db_list_sent(&db, client->account_name, memos,
                                      IRCD_MEMOSERV_LIST_LIMIT, &count)
              : memoserv_db_list(&db, client->account_name, memos,
                                 IRCD_MEMOSERV_LIST_LIMIT, &count)) != 0) {
        memoserv_db_close(&db); ms_notice(server, client, "Memo database is unavailable."); return;
    }
    memoserv_db_close(&db);
    if (count == 0U) { ms_notice(server, client, sent ? "You have no sent memos." : "You have no memos."); return; }
    for (i = 0U; i < count; ++i) {
        if (sent)
            (void)snprintf(line, sizeof(line), "#%lld TO %s %s at %lld",
                           memos[i].id, memos[i].recipient,
                           memos[i].read_at == 0 ? "UNREAD" : "READ", memos[i].created_at);
        else
            (void)snprintf(line, sizeof(line), "#%lld %s from %s at %lld",
                           memos[i].id, memos[i].read_at == 0 ? "UNREAD" : "READ",
                           memos[i].sender, memos[i].created_at);
        ms_notice(server, client, line);
    }
}

static void command_read(Server *server, Client *client, char *params) {
    MemoServDb db = {0}; MemoServMemo memo; long long id; char line[IRCD_OUTPUT_BUFFER_SIZE]; int found;
    char *id_text = params != NULL ? strtok(params, " ") : NULL;
    if (!require_account(server, client)) return;
    if (parse_id(id_text, &id) != 0) { ms_notice(server, client, "Syntax: READ <memo-id>"); return; }
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) { ms_notice(server, client, "Memo database is unavailable."); return; }
    purge_expired(server, &db);
    found = memoserv_db_get(&db, client->account_name, id, &memo);
    if (found != 1) { memoserv_db_close(&db); ms_notice(server, client, found == 0 ? "No such memo." : "Memo lookup failed."); return; }
    (void)memoserv_db_mark_read(&db, client->account_name, id, (long long)time(NULL));
    memoserv_db_close(&db);
    (void)snprintf(line, sizeof(line), "Memo #%lld from %s at %lld: %s", memo.id, memo.sender, memo.created_at, memo.text);
    ms_notice(server, client, line);
}

static void command_delete(Server *server, Client *client, char *params) {
    MemoServDb db = {0}; char *what = params != NULL ? strtok(params, " ") : NULL; long long id; int rc;
    if (!require_account(server, client)) return;
    if (what == NULL) { ms_notice(server, client, "Syntax: DEL <memo-id|ALL>"); return; }
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) { ms_notice(server, client, "Memo database is unavailable."); return; }
    if (strcasecmp(what, "ALL") == 0) {
        rc = memoserv_db_delete_all(&db, client->account_name); memoserv_db_close(&db);
        ms_notice(server, client, rc == 0 ? "All memos deleted." : "Memo deletion failed."); return;
    }
    if (parse_id(what, &id) != 0) { memoserv_db_close(&db); ms_notice(server, client, "Syntax: DEL <memo-id|ALL>"); return; }
    rc = memoserv_db_delete(&db, client->account_name, id); memoserv_db_close(&db);
    ms_notice(server, client, rc == 1 ? "Memo deleted." : rc == 0 ? "No such memo." : "Memo deletion failed.");
}

static void command_reply(Server *server, Client *client, char *params) {
    MemoServDb db = {0}; MemoServMemo memo; long long id, new_id = 0; char *id_text, *text; char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    id_text = params != NULL ? strtok(params, " ") : NULL; text = id_text != NULL ? strtok(NULL, "") : NULL;
    if (text != NULL) { while (*text == ' ') ++text; if (*text == ':') ++text; }
    if (parse_id(id_text, &id) != 0 || text == NULL || *text == '\0' || strlen(text) > IRCD_MEMOSERV_TEXT_MAX) {
        ms_notice(server, client, "Syntax: REPLY <memo-id> :<message>"); return;
    }
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) { ms_notice(server, client, "Memo database is unavailable."); return; }
    purge_expired(server, &db);
    if (memoserv_db_get(&db, client->account_name, id, &memo) != 1) { memoserv_db_close(&db); ms_notice(server, client, "No such memo."); return; }
    memoserv_db_close(&db);
    if (store_memo(server, client, memo.sender, text, &new_id)) {
        (void)snprintf(line, sizeof(line), "Reply memo #%lld sent to %s.", new_id, memo.sender); ms_notice(server, client, line);
    }
}

static void command_forward(Server *server, Client *client, char *params) {
    MemoServDb db = {0}; MemoServMemo memo; long long id, new_id = 0; char *id_text, *account; char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    id_text = params != NULL ? strtok(params, " ") : NULL; account = id_text != NULL ? strtok(NULL, " ") : NULL;
    if (parse_id(id_text, &id) != 0 || account == NULL) { ms_notice(server, client, "Syntax: FORWARD <memo-id> <account>"); return; }
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) { ms_notice(server, client, "Memo database is unavailable."); return; }
    purge_expired(server, &db);
    if (memoserv_db_get(&db, client->account_name, id, &memo) != 1) { memoserv_db_close(&db); ms_notice(server, client, "No such memo."); return; }
    memoserv_db_close(&db);
    if (store_memo(server, client, account, memo.text, &new_id)) {
        (void)snprintf(line, sizeof(line), "Memo #%lld forwarded to %s.", new_id, account); ms_notice(server, client, line);
    }
}

void memoserv_handle_message(Server *server, Client *client, char *text) {
    char *command, *params;
    if (server == NULL || client == NULL || text == NULL) return;
    command = strtok(text, " "); params = strtok(NULL, ""); if (command == NULL) return;
    if (strcasecmp(command, "SEND") == 0) command_send(server, client, params);
    else if (strcasecmp(command, "LIST") == 0) list_records(server, client, 0);
    else if (strcasecmp(command, "SENT") == 0) list_records(server, client, 1);
    else if (strcasecmp(command, "READ") == 0) command_read(server, client, params);
    else if (strcasecmp(command, "REPLY") == 0) command_reply(server, client, params);
    else if (strcasecmp(command, "FORWARD") == 0) command_forward(server, client, params);
    else if (strcasecmp(command, "DEL") == 0 || strcasecmp(command, "DELETE") == 0) command_delete(server, client, params);
    else if (strcasecmp(command, "STATUS") == 0) command_status(server, client);
    else if (strcasecmp(command, "HELP") == 0)
        ms_notice(server, client, "Commands: SEND, LIST, SENT, READ, REPLY, FORWARD, DEL, STATUS, HELP");
    else ms_notice(server, client, "Unknown MemoServ command. Use /MEMOSERV HELP.");
}
