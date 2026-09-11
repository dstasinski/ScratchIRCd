/**
 * @file nickserv_admin.c
 * @brief Network-administrator management of NickServ accounts.
 */

#include "commands.h"
#include "chanserv.h"
#include "chanserv_db.h"
#include "ircv3.h"
#include "message_policy.h"
#include "memoserv_db.h"
#include "modes.h"
#include "nickserv_db.h"
#include "numerics.h"
#include "usermode_policy.h"

#include <argon2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <sys/types.h>

typedef struct ChanServFounderWorkItem {
    char channel[IRC_CHANNEL_NAME_MAX + 1U];
    char successor[IRC_NICK_MAX + 1U];
} ChanServFounderWorkItem;

typedef struct ChanServReferenceWorkItem {
    char channel[IRC_CHANNEL_NAME_MAX + 1U];
} ChanServReferenceWorkItem;

static int require_netadmin(Server *server, Client *client) {
    if (!client_mode_has(client->modes, CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return 1;
    }
    return 0;
}

static void notice(Server *server, Client *client, const char *text) {
    int prefix_length;
    size_t payload_limit;
    size_t text_length;
    size_t offset = 0U;

    if (server == NULL || client == NULL || text == NULL) return;
    prefix_length = snprintf(NULL, 0, ":%s NOTICE %s :",
                             server->config.server_name, client->nick);
    if (prefix_length < 0 || (size_t)prefix_length >= IRC_LINE_CONTENT_MAX)
        return;
    payload_limit = IRC_LINE_CONTENT_MAX - (size_t)prefix_length;
    text_length = strlen(text);

    if (text_length == 0U) {
        client_sendf(client, ":%s NOTICE %s :",
                     server->config.server_name, client->nick);
        return;
    }

    while (offset < text_length && !client->output_overflowed) {
        size_t remaining = text_length - offset;
        size_t chunk = remaining < payload_limit ? remaining : payload_limit;
        client_sendf(client, ":%s NOTICE %s :%.*s",
                     server->config.server_name, client->nick,
                     (int)chunk, text + offset);
        offset += chunk;
    }
}

static void broadcast_registration_mode(Server *server, Channel *channel, int adding) {
    char message[IRCD_MESSAGE_BUFFER_SIZE];
    if (server == NULL || channel == NULL) return;
    (void)snprintf(message, sizeof(message),
                   ":ChanServ!service@%s MODE %s %cr\r\n",
                   server->config.server_name, channel->name,
                   adding ? '+' : '-');
    channel_broadcast(channel, NULL, message);
}

static int hash_password(const char *password, char *encoded, size_t encoded_size) {
    uint8_t salt[IRCD_ARGON2_SALT_BYTES];
    size_t offset = 0U;
    while (offset < sizeof(salt)) {
        ssize_t got = getrandom(salt + offset, sizeof(salt) - offset, 0);
        if (got <= 0) return -1;
        offset += (size_t)got;
    }
    return argon2id_hash_encoded(IRCD_ARGON2_TIME_COST,
                                 IRCD_ARGON2_MEMORY_COST_KIB,
                                 IRCD_ARGON2_PARALLELISM,
                                 password, strlen(password),
                                 salt, sizeof(salt),
                                 IRCD_ARGON2_HASH_BYTES,
                                 encoded, encoded_size) == ARGON2_OK ? 0 : -1;
}

static int valid_vhost(const char *vhost) {
    if (vhost == NULL || strlen(vhost) > IRC_HOST_MAX) return 0;
    return strpbrk(vhost, " \t\r\n") == NULL;
}

static int valid_email(const char *email) {
    const char *at;
    const char *dot;
    if (email == NULL || strlen(email) > IRCD_EMAIL_MAX || strpbrk(email, " \t\r\n") != NULL)
        return 0;
    if (*email == '\0') return 1;
    at = strchr(email, '@');
    if (at == NULL || at == email || strchr(at + 1, '@') != NULL || at[1] == '\0') return 0;
    dot = strrchr(at + 1, '.');
    return dot != NULL && dot != at + 1 && dot[1] != '\0';
}

static int valid_account_name(const char *account) {
    size_t length;
    if (account == NULL) return 0;
    length = strlen(account);
    return length > 0U && length <= IRC_NICK_MAX && strpbrk(account, " \t\r\n") == NULL;
}

static int live_account_matches(const Client *client, const char *account) {
    return client != NULL && valid_account_name(account) &&
           client->account_name[0] != '\0' &&
           strcasecmp(client->account_name, account) == 0;
}

static void clear_live_account(Server *server, const char *account) {
    size_t i;
    if (server == NULL || !valid_account_name(account)) return;
    for (i = 0U; i < server->client_count; ++i) {
        Client *live = server->clients[i];
        if (!live_account_matches(live, account)) continue;
        live->account_name[0] = '\0';
        live->modes = client_mode_remove(live->modes, CLIENT_MODE_REGISTERED);
        live->sasl_state = CLIENT_SASL_NONE;
        if (live->account_vhost_active) {
            live->account_vhost_active = 0;
            live->modes = client_mode_remove(live->modes, CLIENT_MODE_VHOST);
            usermode_apply_cloak(server, live);
        }
        ircv3_account_notify(live);
        chanserv_sync_client_privileges(server, live);
    }
}

static int nickserv_account_enabled(Server *server, const char *account_name) {
    NickServDb db = {0};
    NickServAccount account;
    int found;
    if (server == NULL || !valid_account_name(account_name)) return 0;
    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) return 0;
    found = nickserv_db_get(&db, account_name, &account);
    nickserv_db_close(&db);
    return found == 1 && account.enabled;
}

static int copy_sqlite_text(sqlite3_stmt *stmt, int column,
                            char *destination, size_t destination_size) {
    const unsigned char *raw;
    int bytes;
    if (stmt == NULL || destination == NULL || destination_size == 0U) return -1;
    raw = sqlite3_column_text(stmt, column);
    bytes = sqlite3_column_bytes(stmt, column);
    if (raw == NULL || bytes < 0 || (size_t)bytes >= destination_size ||
        memchr(raw, '\0', (size_t)bytes) != NULL ||
        memchr(raw, '\r', (size_t)bytes) != NULL ||
        memchr(raw, '\n', (size_t)bytes) != NULL)
        return -1;
    memcpy(destination, raw, (size_t)bytes);
    destination[bytes] = '\0';
    return 0;
}

static int collect_founded_channels(ChanServDb *db, const char *founder,
                                    ChanServFounderWorkItem **items,
                                    size_t *count) {
    sqlite3_stmt *stmt = NULL;
    ChanServFounderWorkItem *list = NULL;
    size_t used = 0U;
    size_t capacity = 0U;
    int rc;

    if (db == NULL || db->db == NULL || !valid_account_name(founder) ||
        items == NULL || count == NULL)
        return -1;
    *items = NULL;
    *count = 0U;

    if (sqlite3_prepare_v2(db->db,
        "SELECT name,successor FROM channels WHERE enabled=1 AND founder=?1",
        -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, founder, -1, SQLITE_TRANSIENT);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ChanServFounderWorkItem item;
        if (copy_sqlite_text(stmt, 0, item.channel, sizeof(item.channel)) != 0 ||
            copy_sqlite_text(stmt, 1, item.successor, sizeof(item.successor)) != 0) {
            sqlite3_finalize(stmt);
            free(list);
            return -1;
        }
        if (used == capacity) {
            size_t next_capacity = capacity == 0U ? 8U : capacity * 2U;
            ChanServFounderWorkItem *grown;
            if (next_capacity < capacity) {
                sqlite3_finalize(stmt);
                free(list);
                return -1;
            }
            grown = realloc(list, next_capacity * sizeof(*list));
            if (grown == NULL) {
                sqlite3_finalize(stmt);
                free(list);
                return -1;
            }
            list = grown;
            capacity = next_capacity;
        }
        list[used++] = item;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        free(list);
        return -1;
    }
    *items = list;
    *count = used;
    return 0;
}

static int collect_account_reference_channels(ChanServDb *db, const char *account,
                                              ChanServReferenceWorkItem **items,
                                              size_t *count) {
    sqlite3_stmt *stmt = NULL;
    ChanServReferenceWorkItem *list = NULL;
    size_t used = 0U;
    size_t capacity = 0U;
    int rc;

    if (db == NULL || db->db == NULL || !valid_account_name(account) ||
        items == NULL || count == NULL)
        return -1;
    *items = NULL;
    *count = 0U;

    if (sqlite3_prepare_v2(db->db,
        "SELECT name FROM channels WHERE successor=?1 "
        "UNION SELECT channel FROM access WHERE account=?1",
        -1, &stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_text(stmt, 1, account, -1, SQLITE_TRANSIENT);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        ChanServReferenceWorkItem item;
        if (copy_sqlite_text(stmt, 0, item.channel, sizeof(item.channel)) != 0) {
            sqlite3_finalize(stmt);
            free(list);
            return -1;
        }
        if (used == capacity) {
            size_t next_capacity = capacity == 0U ? 8U : capacity * 2U;
            ChanServReferenceWorkItem *grown;
            if (next_capacity < capacity) {
                sqlite3_finalize(stmt);
                free(list);
                return -1;
            }
            grown = realloc(list, next_capacity * sizeof(*list));
            if (grown == NULL) {
                sqlite3_finalize(stmt);
                free(list);
                return -1;
            }
            list = grown;
            capacity = next_capacity;
        }
        list[used++] = item;
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        free(list);
        return -1;
    }
    *items = list;
    *count = used;
    return 0;
}

static void refresh_chanserv_channel(Server *server, const char *name) {
    Channel *channel;
    int had_registered;
    int has_registered;

    if (server == NULL || name == NULL) return;
    channel = hash_get(&server->channels_by_name, name);
    if (channel == NULL) return;
    had_registered = channel_mode_has(channel->modes, CHANNEL_MODE_REGISTERED);
    chanserv_refresh_channel(server, channel);
    has_registered = channel_mode_has(channel->modes, CHANNEL_MODE_REGISTERED);
    if (had_registered != has_registered)
        broadcast_registration_mode(server, channel, has_registered);
    if (has_registered)
        chanserv_sync_channel_privileges(server, channel);
}

static void handle_chanserv_founder_account_removed(Server *server,
                                                    const char *account_name) {
    ChanServDb db = {0};
    ChanServFounderWorkItem *items = NULL;
    size_t count = 0U;
    size_t i;

    if (server == NULL || !valid_account_name(account_name)) return;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return;
    if (collect_founded_channels(&db, account_name, &items, &count) != 0) {
        chanserv_db_close(&db);
        return;
    }

    for (i = 0U; i < count; ++i) {
        ChanServFounderWorkItem *item = &items[i];
        if (nickserv_account_enabled(server, item->successor) &&
            strcasecmp(item->successor, account_name) != 0) {
            if (chanserv_db_set_founder(&db, item->channel, item->successor) == 0 &&
                chanserv_db_set_successor(&db, item->channel, "") == 0) {
                snotice_broadcast(server, SNOTICE_SERVICES,
                                  "ChanServ successor promoted: channel=%s old_founder=%s new_founder=%s",
                                  item->channel, account_name, item->successor);
                refresh_chanserv_channel(server, item->channel);
            }
        } else if (chanserv_db_set_enabled(&db, item->channel, 0) == 0) {
            snotice_broadcast(server, SNOTICE_SERVICES,
                              "ChanServ registration disabled: channel=%s orphaned_founder=%s",
                              item->channel, account_name);
            refresh_chanserv_channel(server, item->channel);
        }
    }

    free(items);
    chanserv_db_close(&db);
}

static void remove_chanserv_account_references(Server *server,
                                               const char *account_name) {
    ChanServDb db = {0};
    ChanServReferenceWorkItem *items = NULL;
    sqlite3_stmt *stmt = NULL;
    size_t count = 0U;
    size_t i;
    int ok = 1;
    int transaction_started = 0;

    if (server == NULL || !valid_account_name(account_name)) return;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return;
    if (collect_account_reference_channels(&db, account_name, &items, &count) != 0) {
        chanserv_db_close(&db);
        return;
    }

    if (sqlite3_exec(db.db, "BEGIN IMMEDIATE", NULL, NULL, NULL) != SQLITE_OK)
        ok = 0;
    else
        transaction_started = 1;

    if (ok && sqlite3_prepare_v2(db.db,
        "UPDATE channels SET successor='',updated_at=unixepoch() WHERE successor=?1",
        -1, &stmt, NULL) != SQLITE_OK)
        ok = 0;
    if (ok) {
        sqlite3_bind_text(stmt, 1, account_name, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) ok = 0;
    }
    if (stmt != NULL) {
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    if (ok && sqlite3_prepare_v2(db.db,
        "DELETE FROM access WHERE account=?1",
        -1, &stmt, NULL) != SQLITE_OK)
        ok = 0;
    if (ok) {
        sqlite3_bind_text(stmt, 1, account_name, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) != SQLITE_DONE) ok = 0;
    }
    if (stmt != NULL) {
        sqlite3_finalize(stmt);
        stmt = NULL;
    }

    if (transaction_started) {
        if (ok) {
            if (sqlite3_exec(db.db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
                ok = 0;
                (void)sqlite3_exec(db.db, "ROLLBACK", NULL, NULL, NULL);
            }
        } else {
            (void)sqlite3_exec(db.db, "ROLLBACK", NULL, NULL, NULL);
        }
    }
    chanserv_db_close(&db);

    if (!ok) {
        free(items);
        return;
    }
    for (i = 0U; i < count; ++i)
        refresh_chanserv_channel(server, items[i].channel);
    free(items);
}

static void remove_memoserv_account_references(Server *server,
                                               const char *account_name) {
    MemoServDb db = {0};
    size_t deleted = 0U;
    if (server == NULL || !valid_account_name(account_name)) return;
    if (memoserv_db_open(&db, server->config.memoserv_db) != 0) return;
    if (memoserv_db_delete_account(&db, account_name, &deleted) == 0 && deleted != 0U) {
        snotice_broadcast(server, SNOTICE_SERVICES,
                          "MemoServ rows purged: account=%s deleted=%zu",
                          account_name, deleted);
    }
    memoserv_db_close(&db);
}

CommandResult command_nsinfo(Server *server, Client *client, char *params) {
    NickServDb db = {0};
    NickServAccount account;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    char *name;

    if (command_require_registered(client) || require_netadmin(server, client))
        return COMMAND_KEEP_CLIENT;
    name = params != NULL ? strtok(params, " ") : NULL;
    if (name == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "NSINFO");
        return COMMAND_KEEP_CLIENT;
    }

    if (nickserv_db_open(&db, server->config.nickserv_db) != 0 ||
        nickserv_db_get(&db, name, &account) != 1) {
        nickserv_db_close(&db);
        notice(server, client, "No such NickServ account.");
        return COMMAND_KEEP_CLIENT;
    }
    nickserv_db_close(&db);

    (void)snprintf(line, sizeof(line),
                   "NICKSERV %s enabled=%d vhost=%s email=%s email_verified=%d created=%lld updated=%lld",
                   account.name, account.enabled,
                   account.vhost[0] != '\0' ? account.vhost : "-",
                   account.email[0] != '\0' ? account.email : "-",
                   account.email_verified,
                   account.created_at, account.updated_at);
    notice(server, client, line);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_nsset(Server *server, Client *client, char *params) {
    NickServDb db = {0};
    char canonical_name[IRC_NICK_MAX + 1U] = "";
    char *name;
    char *field;
    char *value;
    int disabling = 0;
    int rc = -1;

    if (command_require_registered(client) || require_netadmin(server, client))
        return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "NSSET");
        return COMMAND_KEEP_CLIENT;
    }
    name = strtok(params, " ");
    field = strtok(NULL, " ");
    value = strtok(NULL, "");
    if (value != NULL) while (*value == ' ') ++value;
    if (name == NULL || field == NULL || value == NULL || *value == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "NSSET");
        return COMMAND_KEEP_CLIENT;
    }

    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) {
        notice(server, client, "NSSET failed.");
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(field, "PASSWORD") == 0) {
        char encoded[IRCD_OPER_HASH_MAX + 1U];
        if (hash_password(value, encoded, sizeof(encoded)) == 0)
            rc = nickserv_db_set_password(&db, name, encoded);
    } else if (strcasecmp(field, "VHOST") == 0) {
        const char *vhost = strcmp(value, "-") == 0 ? "" : value;
        if (valid_vhost(vhost)) rc = nickserv_db_set_vhost(&db, name, vhost);
    } else if (strcasecmp(field, "EMAIL") == 0) {
        const char *email = strcmp(value, "-") == 0 ? "" : value;
        if (valid_email(email))
            rc = nickserv_db_admin_set_email(&db, name, email, email[0] != '\0');
    } else if (strcasecmp(field, "ENABLED") == 0) {
        NickServAccount account;
        if ((strcmp(value, "0") == 0 || strcmp(value, "1") == 0) &&
            nickserv_db_get(&db, name, &account) == 1) {
            (void)snprintf(canonical_name, sizeof(canonical_name), "%s", account.name);
            disabling = account.enabled && value[0] == '0';
            rc = nickserv_db_set_enabled(&db, account.name, value[0] == '1');
        }
    }
    nickserv_db_close(&db);
    notice(server, client, rc == 0 ? "NickServ account updated." : "NSSET failed.");
    if (rc == 0) {
        const char *account_name = canonical_name[0] != '\0' ? canonical_name : name;
        const char *detail = strcasecmp(field, "PASSWORD") == 0 ? "PASSWORD changed" :
                             strcasecmp(field, "EMAIL") == 0 ? "EMAIL changed" : value;
        if (disabling) {
            clear_live_account(server, account_name);
            handle_chanserv_founder_account_removed(server, account_name);
        }
        snotice_broadcast(server, SNOTICE_SERVICES,
                          "NSSET by %s: account=%s field=%s value=%s",
                          client->nick, account_name, field, detail);
    }
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_nsdrop(Server *server, Client *client, char *params) {
    NickServDb db = {0};
    NickServAccount account;
    char *name;
    char canonical_name[IRC_NICK_MAX + 1U];

    if (command_require_registered(client) || require_netadmin(server, client))
        return COMMAND_KEEP_CLIENT;
    name = params != NULL ? strtok(params, " ") : NULL;
    if (name == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "NSDROP");
        return COMMAND_KEEP_CLIENT;
    }
    if (nickserv_db_open(&db, server->config.nickserv_db) != 0 ||
        nickserv_db_get(&db, name, &account) != 1 ||
        nickserv_db_delete(&db, account.name) != 0) {
        nickserv_db_close(&db);
        notice(server, client, "NSDROP failed.");
        return COMMAND_KEEP_CLIENT;
    }
    (void)snprintf(canonical_name, sizeof(canonical_name), "%s", account.name);
    nickserv_db_close(&db);
    clear_live_account(server, canonical_name);
    handle_chanserv_founder_account_removed(server, canonical_name);
    remove_chanserv_account_references(server, canonical_name);
    remove_memoserv_account_references(server, canonical_name);
    notice(server, client, "NickServ account deleted.");
    snotice_broadcast(server, SNOTICE_SERVICES,
                      "NSDROP by %s: account=%s", client->nick, canonical_name);
    return COMMAND_KEEP_CLIENT;
}
