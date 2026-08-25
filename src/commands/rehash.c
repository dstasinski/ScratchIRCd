/**
 * @file rehash.c
 * @brief Reload runtime configuration without restarting listeners.
 */

#include "commands.h"
#include "numerics.h"
#include "oper.h"
#include "runtime_config.h"

#include <string.h>

/**
 * Return non-zero when applying updated in-place would leave initialized
 * subsystems or pre-registration clients governed by mixed old/new policy.
 */
static int restart_required(const ServerConfig *current,
                            const ServerConfig *updated,
                            size_t live_clients) {
    if (current == NULL || updated == NULL) return 1;

    /* Listener/TLS/GeoIP objects are constructed only during server_init(). */
    if (strcmp(updated->bind_address, current->bind_address) != 0 ||
        strcmp(updated->port, current->port) != 0 ||
        strcmp(updated->tls_port, current->tls_port) != 0 ||
        strcmp(updated->tls_cert_file, current->tls_cert_file) != 0 ||
        strcmp(updated->tls_key_file, current->tls_key_file) != 0 ||
        strcmp(updated->geoip_city_db, current->geoip_city_db) != 0 ||
        strcmp(updated->geoip_asn_db, current->geoip_asn_db) != 0 ||
        strcmp(updated->server_name, current->server_name) != 0)
        return 1;

    /* Persistent stores must not be switched underneath live runtime state. */
    if (strcmp(updated->operators_db, current->operators_db) != 0 ||
        strcmp(updated->bans_db, current->bans_db) != 0 ||
        strcmp(updated->nickserv_db, current->nickserv_db) != 0 ||
        strcmp(updated->chanserv_db, current->chanserv_db) != 0 ||
        strcmp(updated->memoserv_db, current->memoserv_db) != 0 ||
        strcmp(updated->history_db, current->history_db) != 0)
        return 1;

    /* Registration gates cannot safely change for already-accepted sockets. */
    if (updated->nospoof_enabled != current->nospoof_enabled ||
        updated->nospoof_timeout_seconds != current->nospoof_timeout_seconds ||
        strcmp(updated->server_password, current->server_password) != 0)
        return 1;

    /* Never shrink the global table below the number of live sockets. */
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
        return COMMAND_KEEP_CLIENT;
    }

    runtime_config_defaults(&updated);
    if (runtime_config_load(&updated, server->config.source_path) != 0) {
        client_sendf(client, ":%s NOTICE %s :REHASH failed: configuration error",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    if (restart_required(&server->config, &updated, server->client_count)) {
        client_sendf(client,
                     ":%s NOTICE %s :REHASH rejected: startup-bound, persistent-store, or registration-gate change requires RESTART",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    server->config = updated;
    client_sendf(client, RPL_REHASHING,
                 server->config.server_name, client->nick, server->config.source_path);
    return COMMAND_KEEP_CLIENT;
}
