/**
 * @file operator_admin.c
 * @brief Network-administrator management of operators.db through IRC.
 *
 * Commands:
 *   OPERADD <name> <password> <vhost|-> :<permissions|->
 *   OPERDEL <name>
 *   OPERSET <name> NAME <newname>
 *   OPERSET <name> PASSWORD <password>
 *   OPERSET <name> PERMISSIONS :<permissions|->
 *   OPERSET <name> VHOST <vhost|->
 *   OPERSET <name> ENABLED <0|1>
 *   OPERLIST [name]
 *
 * Only the bootstrap network administrator (+N) may use these commands.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"
#include "oper.h"
#include "operator_db.h"

#include <argon2.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>

static int require_netadmin(Server *server, Client *client) {
    if (!client_mode_has(client->modes, CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return 1;
    }
    return 0;
}

static void admin_notice(Server *server, Client *client, const char *text) {
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

static int valid_oper_name(const Server *server, const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    if (name == NULL || *name == '\0' || strlen(name) > IRCD_OPER_NAME_MAX)
        return 0;
    if (server->config.netadmin_name[0] != '\0' &&
        strcasecmp(name, server->config.netadmin_name) == 0)
        return 0;
    for (; *p != '\0'; ++p)
        if (!isalnum(*p) && *p != '-' && *p != '_' && *p != '.') return 0;
    return 1;
}

static int valid_oper_vhost(const char *host) {
    const unsigned char *p = (const unsigned char *)host;
    if (host == NULL || strlen(host) > IRCD_OPER_VHOST_MAX) return 0;
    for (; *p != '\0'; ++p)
        if (!isalnum(*p) && *p != '.' && *p != '-' && *p != '_' && *p != ':') return 0;
    return 1;
}

/** Validate an ordinary-oper permission string and forbid netadmin. */
static int normalized_permissions(const char *input, const char **output) {
    OperPermissionSet permissions;
    const char *text;

    if (input == NULL || output == NULL) return 0;
    text = strcmp(input, "-") == 0 ? "" : input;
    if (strlen(text) > IRCD_OPER_FLAGS_MAX ||
        oper_permissions_parse(text, &permissions) != 0 ||
        oper_permission_has(permissions, OPER_PERMISSION_NETADMIN)) {
        return 0;
    }
    *output = text;
    return 1;
}

CommandResult command_operadd(Server *server, Client *client, char *params) {
    char *name;
    char *password;
    char *vhost;
    char *permissions_arg;
    const char *permissions;
    const char *stored_vhost;
    OperatorRecord record;
    OperatorDb db = {0};

    if (command_require_registered(client) || require_netadmin(server, client))
        return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "OPERADD");
        return COMMAND_KEEP_CLIENT;
    }

    name = strtok(params, " ");
    password = strtok(NULL, " ");
    vhost = strtok(NULL, " ");
    permissions_arg = strtok(NULL, "");
    if (permissions_arg != NULL && *permissions_arg == ':') ++permissions_arg;
    stored_vhost = vhost != NULL && strcmp(vhost, "-") == 0 ? "" : vhost;

    if (!valid_oper_name(server, name) || password == NULL || *password == '\0' ||
        stored_vhost == NULL || !valid_oper_vhost(stored_vhost) ||
        permissions_arg == NULL ||
        !normalized_permissions(permissions_arg, &permissions)) {
        admin_notice(server, client, "OPERADD invalid parameters");
        return COMMAND_KEEP_CLIENT;
    }

    memset(&record, 0, sizeof(record));
    (void)snprintf(record.name, sizeof(record.name), "%s", name);
    (void)snprintf(record.permissions, sizeof(record.permissions), "%s", permissions);
    (void)snprintf(record.vhost, sizeof(record.vhost), "%s", stored_vhost);
    record.enabled = 1;

    if (hash_password(password, record.password_hash,
                      sizeof(record.password_hash)) != 0 ||
        operator_db_open(&db, server->config.operators_db) != 0) {
        operator_db_close(&db);
        admin_notice(server, client, "OPERADD failed");
        return COMMAND_KEEP_CLIENT;
    }
    if (operator_db_add(&db, &record) != 0) {
        operator_db_close(&db);
        admin_notice(server, client, "OPERADD failed (name may already exist)");
        return COMMAND_KEEP_CLIENT;
    }
    operator_db_close(&db);
    admin_notice(server, client, "Operator added");
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_operdel(Server *server, Client *client, char *params) {
    char *name;
    OperatorDb db = {0};

    if (command_require_registered(client) || require_netadmin(server, client))
        return COMMAND_KEEP_CLIENT;
    name = params != NULL ? strtok(params, " ") : NULL;
    if (name == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "OPERDEL");
        return COMMAND_KEEP_CLIENT;
    }
    if (operator_db_open(&db, server->config.operators_db) != 0 ||
        operator_db_delete(&db, name) != 0) {
        operator_db_close(&db);
        admin_notice(server, client, "OPERDEL failed");
        return COMMAND_KEEP_CLIENT;
    }
    operator_db_close(&db);
    admin_notice(server, client, "Operator deleted");
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_operset(Server *server, Client *client, char *params) {
    char *name;
    char *field;
    char *value;
    OperatorDb db = {0};
    int rc = -1;

    if (command_require_registered(client) || require_netadmin(server, client))
        return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "OPERSET");
        return COMMAND_KEEP_CLIENT;
    }

    name = strtok(params, " ");
    field = strtok(NULL, " ");
    value = strtok(NULL, "");
    if (value != NULL && *value == ':') ++value;
    if (name == NULL || field == NULL || value == NULL || *value == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "OPERSET");
        return COMMAND_KEEP_CLIENT;
    }

    if (operator_db_open(&db, server->config.operators_db) != 0) {
        admin_notice(server, client, "OPERSET failed");
        return COMMAND_KEEP_CLIENT;
    }

    if (strcasecmp(field, "NAME") == 0) {
        if (valid_oper_name(server, value))
            rc = operator_db_set_name(&db, name, value);
    } else if (strcasecmp(field, "PASSWORD") == 0) {
        char encoded[IRCD_OPER_HASH_MAX + 1U];
        if (hash_password(value, encoded, sizeof(encoded)) == 0)
            rc = operator_db_set_password(&db, name, encoded);
    } else if (strcasecmp(field, "PERMISSIONS") == 0) {
        const char *permissions;
        if (normalized_permissions(value, &permissions))
            rc = operator_db_set_permissions(&db, name, permissions);
    } else if (strcasecmp(field, "VHOST") == 0) {
        const char *vhost = strcmp(value, "-") == 0 ? "" : value;
        if (valid_oper_vhost(vhost))
            rc = operator_db_set_vhost(&db, name, vhost);
    } else if (strcasecmp(field, "ENABLED") == 0) {
        if (strcmp(value, "0") == 0 || strcmp(value, "1") == 0)
            rc = operator_db_set_enabled(&db, name, value[0] == '1');
    }

    operator_db_close(&db);
    admin_notice(server, client, rc == 0 ? "Operator updated" : "OPERSET failed");
    return COMMAND_KEEP_CLIENT;
}

typedef struct ListContext {
    Server *server;
    Client *client;
} ListContext;

static int list_one(const OperatorRecord *record, void *context) {
    ListContext *ctx = context;
    char line[IRCD_OUTPUT_BUFFER_SIZE];
    (void)snprintf(line, sizeof(line),
                   "OPER %s enabled=%d vhost=%s permissions=%s created=%lld updated=%lld",
                   record->name, record->enabled,
                   record->vhost[0] != '\0' ? record->vhost : "-",
                   record->permissions[0] != '\0' ? record->permissions : "-",
                   record->created_at, record->updated_at);
    admin_notice(ctx->server, ctx->client, line);
    return 0;
}

CommandResult command_operlist(Server *server, Client *client, char *params) {
    OperatorDb db = {0};
    char *name = params != NULL ? strtok(params, " ") : NULL;

    if (command_require_registered(client) || require_netadmin(server, client))
        return COMMAND_KEEP_CLIENT;
    if (operator_db_open(&db, server->config.operators_db) != 0) {
        admin_notice(server, client, "OPERLIST failed");
        return COMMAND_KEEP_CLIENT;
    }

    if (name != NULL) {
        OperatorRecord record;
        if (operator_db_get(&db, name, &record) == 1) {
            ListContext ctx = {server, client};
            (void)list_one(&record, &ctx);
        } else {
            admin_notice(server, client, "No such operator");
        }
    } else {
        ListContext ctx = {server, client};
        if (operator_db_list(&db, list_one, &ctx) != 0)
            admin_notice(server, client, "OPERLIST failed");
    }

    operator_db_close(&db);
    admin_notice(server, client, "End of operator list");
    return COMMAND_KEEP_CLIENT;
}
