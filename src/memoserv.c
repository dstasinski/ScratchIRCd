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

#define MEMOSERV_PURGE_INTERVAL_SECONDS 300

static time_t retention_last_purge;
static unsigned int retention_last_days;
static char retention_last_db_path[IRCD_CONFIG_PATH_MAX + 1U];
static MemoServDb shared_memo_db = {0};
static char shared_memo_path[IRCD_CONFIG_PATH_MAX + 1U];
static NickServDb shared_nick_db = {0};
static char shared_nick_path[IRCD_CONFIG_PATH_MAX + 1U];

static MemoServDb *memo_db(Server *server) {
    if (server == NULL || server->config.memoserv_db[0] == '\0') return NULL;
    if (shared_memo_db.handle != NULL &&
        strcmp(shared_memo_path, server->config.memoserv_db) == 0)
        return &shared_memo_db;
    memoserv_db_close(&shared_memo_db);
    shared_memo_path[0] = '\0';
    if (memoserv_db_open(&shared_memo_db, server->config.memoserv_db) != 0)
        return NULL;
    (void)snprintf(shared_memo_path, sizeof(shared_memo_path), "%s",
                   server->config.memoserv_db);
    return &shared_memo_db;
}

static NickServDb *nick_db(Server *server) {
    if (server == NULL || server->config.nickserv_db[0] == '\0') return NULL;
    if (shared_nick_db.handle != NULL &&
        strcmp(shared_nick_path, server->config.nickserv_db) == 0)
        return &shared_nick_db;
    nickserv_db_close(&shared_nick_db);
    shared_nick_path[0] = '\0';
    if (nickserv_db_open(&shared_nick_db, server->config.nickserv_db) != 0)
        return NULL;
    (void)snprintf(shared_nick_path, sizeof(shared_nick_path), "%s",
                   server->config.nickserv_db);
    return &shared_nick_db;
}

void memoserv_reset_runtime_state(void) {
    memoserv_db_close(&shared_memo_db);
    nickserv_db_close(&shared_nick_db);
    shared_memo_path[0] = '\0';
    shared_nick_path[0] = '\0';
    retention_last_purge = (time_t)0;
    retention_last_days = 0U;
    retention_last_db_path[0] = '\0';
}

static void ms_notice(Server *server, Client *client, const char *text) {
    int prefix_length;
    size_t payload_limit;
    size_t text_length;
    size_t offset = 0U;

    if (server == NULL || client == NULL || text == NULL) return;
    prefix_length = snprintf(NULL, 0, ":MemoServ!service@%s NOTICE %s :",
                             server->config.server_name, client->nick);
    if (prefix_length < 0 || (size_t)prefix_length >= IRC_LINE_CONTENT_MAX)
        return;
    payload_limit = IRC_LINE_CONTENT_MAX - (size_t)prefix_length;
    text_length = strlen(text);

    if (text_length == 0U) {
        client_sendf(client, ":MemoServ!service@%s NOTICE %s :",
                     server->config.server_name, client->nick);
        return;
    }

    while (offset < text_length && !client->output_overflowed) {
        size_t remaining = text_length - offset;
        size_t chunk = remaining < payload_limit ? remaining : payload_limit;
        client_sendf(client, ":MemoServ!service@%s NOTICE %s :%.*s",
                     server->config.server_name, client->nick,
                     (int)chunk, text + offset);
        offset += chunk;
    }
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

/* Retention is maintenance work, not part of the semantics of each individual
 * MemoServ command. Run the global DELETE at most once every five minutes per
 * configured database/retention policy. A path or retention change forces an
 * immediate pass. Failed purges are not cached, so the next operation retries.
 * Runtime throttle state is module-local so the server lifecycle can reset it
 * and make in-process RESTART behave like a fresh process. */
static void purge_expired(Server *server, MemoServDb *db) {
    long long cutoff;
    time_t now;

    if (server == NULL || db == NULL || server->config.memoserv_retention_days == 0U) return;
    now = time(NULL);
    if (retention_last_purge != (time_t)0 && now >= retention_last_purge &&
        (now - retention_last_purge) < MEMOSERV_PURGE_INTERVAL_SECONDS &&
        retention_last_days == server->config.memoserv_retention_days &&
        strcmp(retention_last_db_path, server->config.memoserv_db) == 0)
        return;

    cutoff = (long long)now - (long long)server->config.memoserv_retention_days * 86400LL;
    if (memoserv_db_purge_before(db, NULL, cutoff, NULL) == 0) {
        retention_last_purge = now;
        retention_last_days = server->config.memoserv_retention_days;
        (void)snprintf(retention_last_db_path, sizeof(retention_last_db_path),
                       "%s", server->config.memoserv_db);
    }
}

static int load_enabled_account(Server *server, const char *name, NickServAccount *account) {
    NickServDb *db;
    int found;
    if (server == NULL || name == NULL || account == NULL) return 0;
    db = nick_db(server);
    if (db == NULL) return 0;
    found = nickserv_db_get(db, name, account);
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
    MemoServDb *db;
    size_t unread = 0U;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (server == NULL || client == NULL || client->account_name[0] == '\0') return;
    db = memo_db(server);
    if (db == NULL) return;
    purge_expired(server, db);
    if (memoserv_db_unread_count(db, client->account_name, &unread) == 0 && unread != 0U) {
        (void)snprintf(line, sizeof(line),
                       "You have %zu unread memo%s. Use /MEMOSERV LIST to view them.",
                       unread, unread == 1U ? "" : "s");
        ms_notice(server, client, line);
    }
}

static int store_memo(Server *server, Client *client, const char *recipient_name,
                      const char *text, long long *memo_id) {
    MemoServDb *db;
    NickServAccount recipient;
    size_t count = 0U;
    if (!load_enabled_account(server, recipient_name, &recipient)) {
        ms_notice(server, client, "That NickServ account does not exist or is disabled.");
        return 0;
    }
    db = memo_db(server);
    if (db == NULL) {
        ms_notice(server, client, "Memo database is unavailable.");
        return 0;
    }
    purge_expired(server, db);
    if (memoserv_db_count(db, recipient.name, &count) != 0) {
        ms_notice(server, client, "Memo database is unavailable.");
        return 0;
    }
    if (count >= server->config.memoserv_quota) {
        ms_notice(server, client, "Recipient memo box is full.");
        return 0;
    }
    if (memoserv_db_send(db, client->account_name, recipient.name, text, memo_id) != 0) {
        ms_notice(server, client, "Memo could not be stored.");
        return 0;
    }
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
    MemoServDb *db;
    size_t unread = 0U, total = 0U;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    db = memo_db(server);
    if (db == NULL) {
        ms_notice(server, client, "Memo database is unavailable."); return;
    }
    purge_expired(server, db);
    if (memoserv_db_unread_count(db, client->account_name, &unread) != 0 ||
        memoserv_db_count(db, client->account_name, &total) != 0) {
        ms_notice(server, client, "Memo database is unavailable."); return;
    }
    (void)snprintf(line, sizeof(line), "Memos: %zu/%zu stored, %zu unread.",
                   total, server->config.memoserv_quota, unread);
    ms_notice(server, client, line);
}

static void list_records(Server *server, Client *client, int sent) {
    MemoServDb *db;
    MemoServMemo memos[IRCD_MEMOSERV_LIST_LIMIT];
    size_t count = 0U, i;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    db = memo_db(server);
    if (db == NULL) {
        ms_notice(server, client, "Memo database is unavailable."); return;
    }
    purge_expired(server, db);
    if ((sent ? memoserv_db_list_sent(db, client->account_name, memos,
                                      IRCD_MEMOSERV_LIST_LIMIT, &count)
              : memoserv_db_list(db, client->account_name, memos,
                                 IRCD_MEMOSERV_LIST_LIMIT, &count)) != 0) {
        ms_notice(server, client, "Memo database is unavailable."); return;
    }
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
    MemoServDb *db; MemoServMemo memo; long long id; char line[IRCD_OUTPUT_BUFFER_SIZE]; int found;
    char *id_text = params != NULL ? strtok(params, " ") : NULL;
    if (!require_account(server, client)) return;
    if (parse_id(id_text, &id) != 0) { ms_notice(server, client, "Syntax: READ <memo-id>"); return; }
    db = memo_db(server);
    if (db == NULL) { ms_notice(server, client, "Memo database is unavailable."); return; }
    purge_expired(server, db);
    found = memoserv_db_get(db, client->account_name, id, &memo);
    if (found != 1) { ms_notice(server, client, found == 0 ? "No such memo." : "Memo lookup failed."); return; }
    (void)memoserv_db_mark_read(db, client->account_name, id, (long long)time(NULL));
    (void)snprintf(line, sizeof(line), "Memo #%lld from %s at %lld: %s", memo.id, memo.sender, memo.created_at, memo.text);
    ms_notice(server, client, line);
}

static void command_delete(Server *server, Client *client, char *params) {
    MemoServDb *db; char *what = params != NULL ? strtok(params, " ") : NULL; long long id; int rc;
    if (!require_account(server, client)) return;
    if (what == NULL) { ms_notice(server, client, "Syntax: DEL <memo-id|ALL>"); return; }
    db = memo_db(server);
    if (db == NULL) { ms_notice(server, client, "Memo database is unavailable."); return; }
    if (strcasecmp(what, "ALL") == 0) {
        rc = memoserv_db_delete_all(db, client->account_name);
        ms_notice(server, client, rc == 0 ? "All memos deleted." : "Memo deletion failed."); return;
    }
    if (parse_id(what, &id) != 0) { ms_notice(server, client, "Syntax: DEL <memo-id|ALL>"); return; }
    rc = memoserv_db_delete(db, client->account_name, id);
    ms_notice(server, client, rc == 1 ? "Memo deleted." : rc == 0 ? "No such memo." : "Memo deletion failed.");
}

static void command_reply(Server *server, Client *client, char *params) {
    MemoServDb *db; MemoServMemo memo; long long id, new_id = 0; char *id_text, *text; char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    id_text = params != NULL ? strtok(params, " ") : NULL; text = id_text != NULL ? strtok(NULL, "") : NULL;
    if (text != NULL) { while (*text == ' ') ++text; if (*text == ':') ++text; }
    if (parse_id(id_text, &id) != 0 || text == NULL || *text == '\0' || strlen(text) > IRCD_MEMOSERV_TEXT_MAX) {
        ms_notice(server, client, "Syntax: REPLY <memo-id> :<message>"); return;
    }
    db = memo_db(server);
    if (db == NULL) { ms_notice(server, client, "Memo database is unavailable."); return; }
    purge_expired(server, db);
    if (memoserv_db_get(db, client->account_name, id, &memo) != 1) { ms_notice(server, client, "No such memo."); return; }
    if (store_memo(server, client, memo.sender, text, &new_id)) {
        (void)snprintf(line, sizeof(line), "Reply memo #%lld sent to %s.", new_id, memo.sender); ms_notice(server, client, line);
    }
}

static void command_forward(Server *server, Client *client, char *params) {
    MemoServDb *db; MemoServMemo memo; long long id, new_id = 0; char *id_text, *account; char line[IRCD_OUTPUT_BUFFER_SIZE];
    if (!require_account(server, client)) return;
    id_text = params != NULL ? strtok(params, " ") : NULL; account = id_text != NULL ? strtok(NULL, " ") : NULL;
    if (parse_id(id_text, &id) != 0 || account == NULL) { ms_notice(server, client, "Syntax: FORWARD <memo-id> <account>"); return; }
    db = memo_db(server);
    if (db == NULL) { ms_notice(server, client, "Memo database is unavailable."); return; }
    purge_expired(server, db);
    if (memoserv_db_get(db, client->account_name, id, &memo) != 1) { ms_notice(server, client, "No such memo."); return; }
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
