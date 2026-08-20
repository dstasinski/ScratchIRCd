/**
 * @file oper.c
 * @brief IRC OPER authentication for bootstrap netadmin and SQLite operators.
 *
 * Only the network administrator is configured in ircd.conf. All ordinary
 * IRC operators are loaded from operators.db. Database records are never
 * allowed to grant netadmin, even if the database is edited outside IRC.
 *
 * OPER host authorization is based on the client's real identity, never the
 * public display hostname. Applying an operator vhost changes display_host
 * only; real_ip and real_host remain available to server security policy.
 */

#include "commands.h"
#include "channel_policy.h"
#include "modes.h"
#include "numerics.h"
#include "oper.h"
#include "operator_db.h"

#include <argon2.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int netadmin_host_matches(const Client *client, const char *mask) {
    char host_identity[IRCD_MESSAGE_BUFFER_SIZE];
    char ip_identity[IRCD_MESSAGE_BUFFER_SIZE];

    if (client == NULL || mask == NULL || *mask == '\0') return 0;

    if (client->real_host[0] != '\0') {
        (void)snprintf(host_identity, sizeof(host_identity), "%s!%s@%s",
                       client->nick, client->user, client->real_host);
        if (irc_mask_match(mask, host_identity)) return 1;
    }

    (void)snprintf(ip_identity, sizeof(ip_identity), "%s!%s@%s",
                   client->nick, client->user, client->real_ip);
    return irc_mask_match(mask, ip_identity);
}

static void grant_oper(Client *client, const char *name,
                       OperPermissionSet permissions, const char *vhost) {
    client->oper_permissions = permissions;
    (void)snprintf(client->oper_name, sizeof(client->oper_name), "%s", name);
    client->modes = client_mode_add(client->modes, CLIENT_MODE_OPER);

    if (oper_permission_has(permissions, OPER_PERMISSION_NETADMIN))
        client->modes = client_mode_add(client->modes, CLIENT_MODE_NETADMIN);
    if (oper_permission_has(permissions, OPER_PERMISSION_HELPOP))
        client->modes = client_mode_add(client->modes, CLIENT_MODE_HELPOP);
    if (oper_permission_has(permissions, OPER_PERMISSION_GETHOST) &&
        vhost != NULL && *vhost != '\0') {
        (void)snprintf(client->display_host, sizeof(client->display_host), "%s", vhost);
        client->modes = client_mode_add(client->modes, CLIENT_MODE_VHOST);
    }
}

CommandResult command_oper(Server *server, Client *client, char *params) {
    char *name;
    char *password;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "OPER");
        return COMMAND_KEEP_CLIENT;
    }

    name = strtok(params, " ");
    password = strtok(NULL, "");
    if (password != NULL && *password == ':') ++password;
    if (name == NULL || password == NULL || *password == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "OPER");
        return COMMAND_KEEP_CLIENT;
    }

    if (server->config.netadmin_name[0] != '\0' &&
        strcasecmp(name, server->config.netadmin_name) == 0) {
        if (server->config.netadmin_password_hash[0] == '\0' ||
            !netadmin_host_matches(client, server->config.netadmin_hostmask)) {
            client_sendf(client, ERR_NOOPERHOST,
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (argon2id_verify(server->config.netadmin_password_hash,
                            password, strlen(password)) != ARGON2_OK) {
            client_sendf(client, ERR_PASSWDMISMATCH,
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }

        grant_oper(client, server->config.netadmin_name,
                   OPER_PERMISSION_ALL, server->config.netadmin_vhost);
        client_sendf(client, RPL_YOUREOPER,
                     server->config.server_name, client->nick,
                     " Network Administrator");
        return COMMAND_KEEP_CLIENT;
    }

    {
        OperatorDb db;
        OperatorRecord record;
        OperPermissionSet permissions;
        int found;

        if (operator_db_open(&db, server->config.operators_db) != 0) {
            client_sendf(client, ERR_NOPRIVILEGES,
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        found = operator_db_get(&db, name, &record);
        operator_db_close(&db);

        if (found != 1 || !record.enabled) {
            client_sendf(client, ERR_NOOPERHOST,
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (argon2id_verify(record.password_hash,
                            password, strlen(password)) != ARGON2_OK) {
            client_sendf(client, ERR_PASSWDMISMATCH,
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (oper_permissions_parse(record.permissions, &permissions) != 0 ||
            oper_permission_has(permissions, OPER_PERMISSION_NETADMIN)) {
            client_sendf(client, ERR_NOPRIVILEGES,
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }

        grant_oper(client, record.name, permissions, record.vhost);
        client_sendf(client, RPL_YOUREOPER,
                     server->config.server_name, client->nick,
                     "n IRC operator");
    }

    return COMMAND_KEEP_CLIENT;
}
