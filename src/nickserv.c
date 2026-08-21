/**
 * @file nickserv.c
 * @brief Virtual NickServ implementation.
 *
 * NickServ is not a Client: it is never inserted into client hashes, channel
 * membership, NAMES, WHO, LIST or LUSERS. PRIVMSG targeting "NickServ" is
 * intercepted and handled here.
 */

#include "nickserv.h"
#include "modes.h"
#include "nickserv_db.h"

#include <argon2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>

static void nickserv_notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":NickServ!service@%s NOTICE %s :%s",
                 server->config.server_name, client->nick, text);
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

static void apply_account(Client *client, const NickServAccount *account) {
    (void)snprintf(client->account_name, sizeof(client->account_name), "%s", account->name);
    client->modes = client_mode_add(client->modes, CLIENT_MODE_REGISTERED);

    if (account->vhost[0] != '\0') {
        (void)snprintf(client->display_host, sizeof(client->display_host), "%s", account->vhost);
        client->modes = client_mode_remove(client->modes, CLIENT_MODE_CLOAKED);
        client->modes = client_mode_add(client->modes, CLIENT_MODE_VHOST);
    }
}

int nickserv_identify(Server *server, Client *client,
                      const char *account_name, const char *password) {
    NickServDb db = {0};
    NickServAccount account;
    int found;

    if (server == NULL || client == NULL || account_name == NULL || password == NULL ||
        *account_name == '\0' || *password == '\0') return 0;

    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) return 0;
    found = nickserv_db_get(&db, account_name, &account);
    nickserv_db_close(&db);
    if (found != 1 || !account.enabled) return 0;
    if (argon2id_verify(account.password_hash, password, strlen(password)) != ARGON2_OK) return 0;

    apply_account(client, &account);
    return 1;
}

static void command_register(Server *server, Client *client, char *password) {
    NickServDb db = {0};
    NickServAccount account;
    int existing;

    if (password == NULL || *password == '\0') {
        nickserv_notice(server, client, "Syntax: REGISTER <password>");
        return;
    }
    if (client->nick[0] == '\0') {
        nickserv_notice(server, client, "You must have a nickname before registering.");
        return;
    }
    if (client->account_name[0] != '\0') {
        nickserv_notice(server, client, "You are already identified to an account.");
        return;
    }

    if (nickserv_db_open(&db, server->config.nickserv_db) != 0) {
        nickserv_notice(server, client, "Account database is unavailable.");
        return;
    }
    existing = nickserv_db_get(&db, client->nick, &account);
    if (existing != 0) {
        nickserv_db_close(&db);
        nickserv_notice(server, client, existing == 1
            ? "That nickname is already registered."
            : "Account lookup failed.");
        return;
    }

    memset(&account, 0, sizeof(account));
    (void)snprintf(account.name, sizeof(account.name), "%s", client->nick);
    account.enabled = 1;
    if (hash_password(password, account.password_hash, sizeof(account.password_hash)) != 0 ||
        nickserv_db_add(&db, &account) != 0) {
        nickserv_db_close(&db);
        nickserv_notice(server, client, "Nickname registration failed.");
        return;
    }
    nickserv_db_close(&db);
    apply_account(client, &account);
    nickserv_notice(server, client, "Nickname registered and identified.");
}

static void command_identify(Server *server, Client *client, char *params) {
    char *first;
    char *second;
    const char *account;
    const char *password;

    first = params != NULL ? strtok(params, " ") : NULL;
    second = strtok(NULL, "");
    if (first == NULL) {
        nickserv_notice(server, client, "Syntax: IDENTIFY [nick] <password>");
        return;
    }
    if (second == NULL) {
        account = client->nick;
        password = first;
    } else {
        while (*second == ' ') ++second;
        account = first;
        password = second;
    }

    if (nickserv_identify(server, client, account, password)) {
        nickserv_notice(server, client, "Password accepted - you are now identified.");
    } else {
        nickserv_notice(server, client, "Password incorrect or account unavailable.");
    }
}

static void command_set_password(Server *server, Client *client, char *password) {
    NickServDb db = {0};
    char encoded[IRCD_OPER_HASH_MAX + 1U];

    if (client->account_name[0] == '\0') {
        nickserv_notice(server, client, "You must identify before changing your password.");
        return;
    }
    if (password == NULL || *password == '\0') {
        nickserv_notice(server, client, "Syntax: SET PASSWORD <new-password>");
        return;
    }
    if (hash_password(password, encoded, sizeof(encoded)) != 0 ||
        nickserv_db_open(&db, server->config.nickserv_db) != 0 ||
        nickserv_db_set_password(&db, client->account_name, encoded) != 0) {
        nickserv_db_close(&db);
        nickserv_notice(server, client, "Password change failed.");
        return;
    }
    nickserv_db_close(&db);
    nickserv_notice(server, client, "Password changed.");
}

void nickserv_handle_message(Server *server, Client *client, char *text) {
    char *command;
    char *rest;

    if (server == NULL || client == NULL || text == NULL) return;
    command = strtok(text, " ");
    rest = strtok(NULL, "");
    if (rest != NULL) while (*rest == ' ') ++rest;

    if (command == NULL) return;
    if (strcasecmp(command, "REGISTER") == 0) {
        command_register(server, client, rest);
    } else if (strcasecmp(command, "IDENTIFY") == 0) {
        command_identify(server, client, rest);
    } else if (strcasecmp(command, "SET") == 0) {
        char *field = rest != NULL ? strtok(rest, " ") : NULL;
        char *value = strtok(NULL, "");
        if (field != NULL && value != NULL && strcasecmp(field, "PASSWORD") == 0) {
            while (*value == ' ') ++value;
            command_set_password(server, client, value);
        } else {
            nickserv_notice(server, client, "Syntax: SET PASSWORD <new-password>");
        }
    } else if (strcasecmp(command, "HELP") == 0) {
        nickserv_notice(server, client,
            "Commands: REGISTER <password>, IDENTIFY [nick] <password>, SET PASSWORD <new-password>");
    } else {
        nickserv_notice(server, client, "Unknown command. Use HELP.");
    }
}

int service_nickname_reserved(const char *nick) {
    if (nick == NULL) return 0;
    return strcasecmp(nick, "NickServ") == 0 ||
           strcasecmp(nick, "ChanServ") == 0 ||
           strcasecmp(nick, "MemoServ") == 0;
}
