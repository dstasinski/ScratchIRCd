/**
 * @file rehash.c
 * @brief Reload runtime configuration without restarting listeners.
 */

#include "commands.h"
#include "message_policy.h"
#include "numerics.h"
#include "oper.h"
#include "runtime_config.h"

#include <string.h>

static int restart_required(const ServerConfig *current,
                            const ServerConfig *updated,
                            size_t live_clients) {
    if (current == NULL || updated == NULL) return 1;

    if (strcmp(updated->bind_address, current->bind_address) != 0 ||
        strcmp(updated->port, current->port) != 0 ||
        strcmp(updated->tls_port, current->tls_port) != 0 ||
        strcmp(updated->tls_cert_file, current->tls_cert_file) != 0 ||
        strcmp(updated->tls_key_file, current->tls_key_file) != 0 ||
        strcmp(updated->geoip_city_db, current->geoip_city_db) != 0 ||
        strcmp(updated->geoip_asn_db, current->geoip_asn_db) != 0 ||
        strcmp(updated->server_name, current->server_name) != 0)
        return 1;

    if (strcmp(updated->operators_db, current->operators_db) != 0 ||
        strcmp(updated->bans_db, current->bans_db) != 0 ||
        strcmp(updated->nickserv_db, current->nickserv_db) != 0 ||
        strcmp(updated->chanserv_db, current->chanserv_db) != 0 ||
        strcmp(updated->memoserv_db, current->memoserv_db) != 0 ||
        strcmp(updated->history_db, current->history_db) != 0)
        return 1;

    if (updated->nospoof_enabled != current->nospoof_enabled ||
        updated->nospoof_timeout_seconds != current->nospoof_timeout_seconds ||
        updated->registration_timeout_seconds != current->registration_timeout_seconds ||
        strcmp(updated->server_password, current->server_password) != 0)
        return 1;

    if (updated->max_clients < live_clients) return 1;

    return 0;
}

CommandResult command_rehash(Server *server, Client *client, char *params) {
    ServerConfig updated;
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_REHASH)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (server->config.source_path[0] == '\0') {
        client_sendf(client, ":%s NOTICE %s :REHASH unavailable: no configuration source path",
                     server->config.server_name, client->nick);
        snotice_broadcast(server, SNOTICE_ADMIN,
                          "REHASH unavailable for %s: no configuration source path",
                          client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    runtime_config_defaults(&updated);
    if (runtime_config_load(&updated, server->config.source_path) != 0) {
        client_sendf(client, ":%s NOTICE %s :REHASH failed: configuration error",
                     server->config.server_name, client->nick);
        snotice_broadcast(server, SNOTICE_ADMIN,
                          "REHASH failed for %s: configuration error",
                          client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    if (restart_required(&server->config, &updated, server->client_count)) {
        client_sendf(client,
                     ":%s NOTICE %s :REHASH rejected: startup-bound, persistent-store, or registration-gate change requires RESTART",
                     server->config.server_name, client->nick);
        snotice_broadcast(server, SNOTICE_ADMIN,
                          "REHASH rejected for %s: configuration change requires RESTART",
                          client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    server->config = updated;
    client_sendf(client, RPL_REHASHING,
                 server->config.server_name, client->nick, server->config.source_path);
    snotice_broadcast(server, SNOTICE_ADMIN,
                      "REHASH completed by %s from %s",
                      client->nick, client->real_ip);
    return COMMAND_KEEP_CLIENT;
}
