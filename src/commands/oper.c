/**
 * @file oper.c
 * @brief Implementation of IRC OPER using Argon2id credentials.
 *
 * The bootstrap operator definition lives in ircd.conf until NickServ/SQLite
 * accounts are implemented. Authentication uses the effective IRC identity,
 * which is important for future WebIRC clients: a configured oper host mask
 * applies to the actual end user rather than the gateway socket identity.
 */

#include "commands.h"
#include "channel_policy.h"
#include "modes.h"
#include "numerics.h"
#include "oper.h"

#include <argon2.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/** Return non-zero if the configured mask matches effective host or IP. */
static int oper_host_matches(const Client *client, const char *mask) {
    char host_identity[IRCD_MESSAGE_BUFFER_SIZE];
    char ip_identity[IRCD_MESSAGE_BUFFER_SIZE];

    if (client == NULL || mask == NULL || *mask == '\0') return 0;
    (void)snprintf(host_identity, sizeof(host_identity), "%s!%s@%s",
                   client->nick, client->user, client->host);
    (void)snprintf(ip_identity, sizeof(ip_identity), "%s!%s@%s",
                   client->nick, client->user, client->ip);
    return irc_mask_match(mask, host_identity) || irc_mask_match(mask, ip_identity);
}

CommandResult command_oper(Server *server, Client *client, char *params) {
    char *name;
    char *password;
    OperPermissionSet permissions;

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

    if (server->config.oper_name[0] == '\0' ||
        server->config.oper_password_hash[0] == '\0' ||
        strcasecmp(name, server->config.oper_name) != 0 ||
        !oper_host_matches(client, server->config.oper_hostmask)) {
        client_sendf(client, ERR_NOOPERHOST,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    if (argon2id_verify(server->config.oper_password_hash,
                        password, strlen(password)) != ARGON2_OK) {
        client_sendf(client, ERR_PASSWDMISMATCH,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    if (oper_permissions_parse(server->config.oper_flags, &permissions) != 0) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    client->oper_permissions = permissions;
    (void)snprintf(client->oper_name, sizeof(client->oper_name), "%s",
                   server->config.oper_name);
    client->modes = client_mode_add(client->modes, CLIENT_MODE_OPER);

    if (oper_permission_has(permissions, OPER_PERMISSION_NETADMIN)) {
        client->modes = client_mode_add(client->modes, CLIENT_MODE_NETADMIN);
    }
    if (oper_permission_has(permissions, OPER_PERMISSION_HELPOP)) {
        client->modes = client_mode_add(client->modes, CLIENT_MODE_HELPOP);
    }
    if (oper_permission_has(permissions, OPER_PERMISSION_GETHOST) &&
        server->config.oper_vhost[0] != '\0') {
        (void)snprintf(client->host, sizeof(client->host), "%s",
                       server->config.oper_vhost);
        client->modes = client_mode_add(client->modes, CLIENT_MODE_VHOST);
    }

    client_sendf(client, RPL_YOUREOPER,
                 server->config.server_name, client->nick,
                 oper_permission_has(permissions, OPER_PERMISSION_NETADMIN)
                     ? " Network Administrator" : "n IRC operator");
    return COMMAND_KEEP_CLIENT;
}
