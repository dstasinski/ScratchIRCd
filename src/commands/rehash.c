/**
 * @file rehash.c
 * @brief Reload runtime configuration without restarting listeners.
 */

#include "commands.h"
#include "numerics.h"
#include "oper.h"
#include "runtime_config.h"

#include <string.h>

CommandResult command_rehash(Server *server, Client *client, char *params) {
    ServerConfig updated;
    (void)params;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_REHASH)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
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

    /* Listener/TLS/MMDB identity changes require RESTART so state stays coherent. */
    if (strcmp(updated.bind_address, server->config.bind_address) != 0 ||
        strcmp(updated.port, server->config.port) != 0 ||
        strcmp(updated.tls_port, server->config.tls_port) != 0 ||
        strcmp(updated.tls_cert_file, server->config.tls_cert_file) != 0 ||
        strcmp(updated.tls_key_file, server->config.tls_key_file) != 0 ||
        strcmp(updated.geoip_city_db, server->config.geoip_city_db) != 0 ||
        strcmp(updated.geoip_asn_db, server->config.geoip_asn_db) != 0 ||
        strcmp(updated.server_name, server->config.server_name) != 0 ||
        updated.max_clients < server->client_count) {
        client_sendf(client, ":%s NOTICE %s :REHASH rejected: listener/TLS/GeoIP/server identity change requires restart",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    server->config = updated;
    client_sendf(client, RPL_REHASHING,
                 server->config.server_name, client->nick, server->config.source_path);
    return COMMAND_KEEP_CLIENT;
}
