#ifndef IRCD_RUNTIME_CONFIG_H
#define IRCD_RUNTIME_CONFIG_H

#include <stddef.h>

#include "config.h"

/**
 * Runtime server configuration loaded from ircd.conf.
 *
 * Compile-time limits and safe defaults remain in config.h; settings that an
 * administrator should be able to change without rebuilding the daemon live
 * here. Additional subsystems (TLS, WebIRC, services, databases) will extend
 * this structure as they are implemented.
 */
typedef struct ServerConfig {
    char server_name[IRC_HOST_MAX + 1U];
    char network_name[IRCD_NETWORK_NAME_MAX + 1U];
    char bind_address[IRC_HOST_MAX + 1U];
    char port[IRCD_PORT_TEXT_MAX + 1U];
    size_t max_clients;
    unsigned int dns_timeout_seconds;

    char server_password[IRCD_SERVER_PASSWORD_MAX + 1U];
    char motd_file[IRCD_CONFIG_PATH_MAX + 1U];
    char rules_file[IRCD_CONFIG_PATH_MAX + 1U];
    char admin_location1[IRCD_ADMIN_TEXT_MAX + 1U];
    char admin_location2[IRCD_ADMIN_TEXT_MAX + 1U];
    char admin_email[IRCD_ADMIN_TEXT_MAX + 1U];

    /**
     * Bootstrap IRC operator definition.
     *
     * These fields provide an administrative path before NickServ/SQLite is
     * available. Later, identified registered accounts can populate the same
     * Client.oper_permissions representation from database flags.
     */
    char oper_name[IRCD_OPER_NAME_MAX + 1U];
    char oper_password_hash[IRCD_OPER_HASH_MAX + 1U];
    char oper_hostmask[IRCD_OPER_HOSTMASK_MAX + 1U];
    char oper_flags[IRCD_OPER_FLAGS_MAX + 1U];
    char oper_vhost[IRCD_OPER_VHOST_MAX + 1U];
} ServerConfig;

void runtime_config_defaults(ServerConfig *config);
int runtime_config_load(ServerConfig *config, const char *path);

#endif /* IRCD_RUNTIME_CONFIG_H */
