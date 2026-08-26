#ifndef IRCD_RUNTIME_CONFIG_H
#define IRCD_RUNTIME_CONFIG_H

#include <stddef.h>
#include "config.h"
#include "dnsbl.h"

typedef struct WebIrcGatewayConfig {
    char ip[IRC_IP_MAX + 1U];
    char password[IRCD_WEBIRC_PASSWORD_MAX + 1U];
} WebIrcGatewayConfig;

typedef struct ServerConfig {
    char server_name[IRC_HOST_MAX + 1U];
    char network_name[IRCD_NETWORK_NAME_MAX + 1U];
    char bind_address[IRC_HOST_MAX + 1U];
    char port[IRCD_PORT_TEXT_MAX + 1U];
    size_t max_clients;
    size_t max_connections_per_ip;
    char connection_limit_exempt_ips[IRCD_MAX_CONNECTION_LIMIT_EXEMPT_IPS][IRC_IP_MAX + 1U];
    size_t connection_limit_exempt_ip_count;
    unsigned int registration_timeout_seconds;
    unsigned int dns_timeout_seconds;

    char tls_port[IRCD_PORT_TEXT_MAX + 1U];
    char tls_cert_file[IRCD_CONFIG_PATH_MAX + 1U];
    char tls_key_file[IRCD_CONFIG_PATH_MAX + 1U];

    /* PING-cookie anti-spoofing. Disabled by default for legacy configs; the
     * shipped example enables it. */
    int nospoof_enabled;
    unsigned int nospoof_timeout_seconds;

    WebIrcGatewayConfig webirc_gateways[IRCD_MAX_WEBIRC_GATEWAYS];
    size_t webirc_gateway_count;

    DnsblZone dnsbls[IRCD_MAX_DNSBLS];
    size_t dnsbl_count;
    unsigned int dnsbl_timeout_seconds;

    char geoip_city_db[IRCD_CONFIG_PATH_MAX + 1U];
    char geoip_asn_db[IRCD_CONFIG_PATH_MAX + 1U];

    char server_password[IRCD_SERVER_PASSWORD_MAX + 1U];
    char motd_file[IRCD_CONFIG_PATH_MAX + 1U];
    char rules_file[IRCD_CONFIG_PATH_MAX + 1U];
    char admin_location1[IRCD_ADMIN_TEXT_MAX + 1U];
    char admin_location2[IRCD_ADMIN_TEXT_MAX + 1U];
    char admin_email[IRCD_ADMIN_TEXT_MAX + 1U];

    char operators_db[IRCD_CONFIG_PATH_MAX + 1U];
    char bans_db[IRCD_CONFIG_PATH_MAX + 1U];
    char nickserv_db[IRCD_CONFIG_PATH_MAX + 1U];
    char chanserv_db[IRCD_CONFIG_PATH_MAX + 1U];
    char memoserv_db[IRCD_CONFIG_PATH_MAX + 1U];
    char history_db[IRCD_CONFIG_PATH_MAX + 1U];
    size_t history_limit;
    size_t memoserv_quota;
    unsigned int memoserv_retention_days;

    unsigned int kline_default_duration_seconds;
    unsigned int zline_default_duration_seconds;
    char kline_default_reason[IRC_QUIT_REASON_MAX + 1U];
    char zline_default_reason[IRC_QUIT_REASON_MAX + 1U];

    char sendmail_path[IRCD_CONFIG_PATH_MAX + 1U];
    char mail_from[IRCD_EMAIL_MAX + 1U];
    unsigned int nickserv_reset_seconds;
    unsigned int nickserv_verify_seconds;

    char netadmin_name[IRCD_OPER_NAME_MAX + 1U];
    char netadmin_password_hash[IRCD_OPER_HASH_MAX + 1U];
    char netadmin_hostmask[IRCD_OPER_HOSTMASK_MAX + 1U];
    char netadmin_vhost[IRCD_OPER_VHOST_MAX + 1U];

    char source_path[IRCD_CONFIG_PATH_MAX + 1U];
} ServerConfig;

void runtime_config_defaults(ServerConfig *config);
int runtime_config_load(ServerConfig *config, const char *path);

#endif
