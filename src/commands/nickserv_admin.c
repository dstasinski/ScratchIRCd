/**
 * @file nickserv_admin.c
 * @brief Network-administrator management of NickServ accounts.
 *
 * Commands:
 *   NSINFO <account>
 *   NSSET <account> PASSWORD <new-password>
 *   NSSET <account> VHOST <vhost|->
 *   NSSET <account> ENABLED <0|1>
 *   NSDROP <account>
 */

#include "commands.h"
#include "modes.h"
#include "nickserv_db.h"
#include "numerics.h"

#include <argon2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <sys/types.h>

static int require_netadmin(Server *server, Client *client) {
    if (!client_mode_has(client->modes, CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return 1;
    }
    return 0;
}

static void notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":%s NOTICE %s :%s",
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

static int valid_vhost(const char *vhost) {
    if (vhost == NULL || strlen(vhost) > IRC_HOST_MAX) return 0;
    return strpbrk(vhost, " \t\r\n") == NULL;
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
                   "NICKSERV %s enabled=%d vhost=%s created=%lld updated=%lld",
                   account.name, account.enabled,
                   account.vhost[0] != '\0' ? account.vhost : "-",
                   account.created_at, account.updated_at);
    notice(server, client, line);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_nsset(Server *server, Client *client, char *params) {
    NickServDb db = {0};
    char *name;
    char *field;
    char *value;
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
    } else if (strcasecmp(field, "ENABLED") == 0) {
        if (strcmp(value, "0") == 0 || strcmp(value, "1") == 0)
            rc = nickserv_db_set_enabled(&db, name, value[0] == '1');
    }
    nickserv_db_close(&db);
    notice(server, client, rc == 0 ? "NickServ account updated." : "NSSET failed.");
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_nsdrop(Server *server, Client *client, char *params) {
    NickServDb db = {0};
    char *name;

    if (command_require_registered(client) || require_netadmin(server, client))
        return COMMAND_KEEP_CLIENT;
    name = params != NULL ? strtok(params, " ") : NULL;
    if (name == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "NSDROP");
        return COMMAND_KEEP_CLIENT;
    }
    if (nickserv_db_open(&db, server->config.nickserv_db) != 0 ||
        nickserv_db_delete(&db, name) != 0) {
        nickserv_db_close(&db);
        notice(server, client, "NSDROP failed.");
        return COMMAND_KEEP_CLIENT;
    }
    nickserv_db_close(&db);
    notice(server, client, "NickServ account deleted.");
    return COMMAND_KEEP_CLIENT;
}
