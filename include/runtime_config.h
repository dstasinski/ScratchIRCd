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
    char server_name[IRC_HOST_MAX + 1U];             /**< IRC server name/prefix. */
    char network_name[IRCD_NETWORK_NAME_MAX + 1U];   /**< Displayed network name. */
    char bind_address[IRC_HOST_MAX + 1U];            /**< Empty means all local addresses. */
    char port[IRCD_PORT_TEXT_MAX + 1U];              /**< Plain-text IRC listen port. */
    size_t max_clients;                              /**< Maximum simultaneous clients. */
    unsigned int dns_timeout_seconds;                /**< Registration DNS timeout. */

    char server_password[IRCD_SERVER_PASSWORD_MAX + 1U]; /**< Optional PASS gate. */
    char motd_file[IRCD_CONFIG_PATH_MAX + 1U];       /**< MOTD text file path. */
    char rules_file[IRCD_CONFIG_PATH_MAX + 1U];      /**< RULES text file path. */
    char admin_location1[IRCD_ADMIN_TEXT_MAX + 1U];  /**< ADMIN location line one. */
    char admin_location2[IRCD_ADMIN_TEXT_MAX + 1U];  /**< ADMIN location line two. */
    char admin_email[IRCD_ADMIN_TEXT_MAX + 1U];      /**< ADMIN contact address. */
} ServerConfig;

/** Populate config with compile-time defaults from config.h. */
void runtime_config_defaults(ServerConfig *config);

/**
 * Load simple key=value overrides from path.
 *
 * Blank lines and lines beginning with '#' are ignored. Unknown keys are
 * rejected so spelling mistakes cannot silently change server behavior.
 * Returns 0 on success and -1 on an I/O or validation error.
 */
int runtime_config_load(ServerConfig *config, const char *path);

#endif /* IRCD_RUNTIME_CONFIG_H */
