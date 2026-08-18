#ifndef IRCD_RUNTIME_CONFIG_H
#define IRCD_RUNTIME_CONFIG_H

#include <stddef.h>

#include "config.h"

/**
 * Runtime server configuration loaded from ircd.conf.
 *
 * Ordinary IRC operator accounts are intentionally excluded from this
 * structure and live in operators.db. Only the bootstrap network
 * administrator remains in ircd.conf.
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

    char operators_db[IRCD_CONFIG_PATH_MAX + 1U];
    char bans_db[IRCD_CONFIG_PATH_MAX + 1U];

    char netadmin_name[IRCD_OPER_NAME_MAX + 1U];
    char netadmin_password_hash[IRCD_OPER_HASH_MAX + 1U];
    char netadmin_hostmask[IRCD_OPER_HOSTMASK_MAX + 1U];
    char netadmin_vhost[IRCD_OPER_VHOST_MAX + 1U];

    /** Source file used by REHASH; not itself a parsed configuration key. */
    char source_path[IRCD_CONFIG_PATH_MAX + 1U];
} ServerConfig;

void runtime_config_defaults(ServerConfig *config);
int runtime_config_load(ServerConfig *config, const char *path);

#endif /* IRCD_RUNTIME_CONFIG_H */
