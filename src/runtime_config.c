/** @file runtime_config.c @brief Strict key=value runtime configuration loading. */
#include "runtime_config.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *text) {
    char *end;
    while (*text != '\0' && isspace((unsigned char)*text)) ++text;
    if (*text == '\0') return text;
    end = text + strlen(text) - 1U;
    while (end > text && isspace((unsigned char)*end)) *end-- = '\0';
    return text;
}

static int copy_value(char *dest, size_t size, const char *value) {
    int written = snprintf(dest, size, "%s", value);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

void runtime_config_defaults(ServerConfig *config) {
    if (config == NULL) return;
    memset(config, 0, sizeof(*config));
    (void)copy_value(config->server_name, sizeof(config->server_name), IRCD_DEFAULT_SERVER_NAME);
    (void)copy_value(config->network_name, sizeof(config->network_name), IRCD_DEFAULT_NETWORK_NAME);
    (void)copy_value(config->bind_address, sizeof(config->bind_address), IRCD_DEFAULT_BIND_ADDRESS);
    (void)copy_value(config->port, sizeof(config->port), IRCD_DEFAULT_PORT);
    (void)copy_value(config->tls_port, sizeof(config->tls_port), IRCD_DEFAULT_TLS_PORT);
    (void)copy_value(config->motd_file, sizeof(config->motd_file), IRCD_DEFAULT_MOTD_FILE);
    (void)copy_value(config->rules_file, sizeof(config->rules_file), IRCD_DEFAULT_RULES_FILE);
    (void)copy_value(config->operators_db, sizeof(config->operators_db), IRCD_DEFAULT_OPERATORS_DB);
    (void)copy_value(config->bans_db, sizeof(config->bans_db), IRCD_DEFAULT_BANS_DB);
    (void)copy_value(config->nickserv_db, sizeof(config->nickserv_db), IRCD_DEFAULT_NICKSERV_DB);
    (void)copy_value(config->geoip_city_db, sizeof(config->geoip_city_db), IRCD_DEFAULT_GEOIP_CITY_DB);
    (void)copy_value(config->geoip_asn_db, sizeof(config->geoip_asn_db), IRCD_DEFAULT_GEOIP_ASN_DB);
    (void)copy_value(config->netadmin_hostmask, sizeof(config->netadmin_hostmask), "*!*@*");
    config->max_clients = IRCD_DEFAULT_MAX_CLIENTS;
    config->dns_timeout_seconds = IRCD_DEFAULT_DNS_TIMEOUT_SECONDS;
    config->dnsbl_timeout_seconds = IRCD_DEFAULT_DNSBL_TIMEOUT_SECONDS;
}

static int add_webirc_gateway(ServerConfig *config, const char *value) {
    char copy[IRCD_CONFIG_LINE_MAX];
    char *ip, *password;
    struct in_addr v4; struct in6_addr v6;
    WebIrcGatewayConfig *gateway;
    if (config->webirc_gateway_count >= IRCD_MAX_WEBIRC_GATEWAYS || strlen(value) >= sizeof(copy)) return -1;
    (void)snprintf(copy, sizeof(copy), "%s", value);
    ip = strtok(copy, " \t"); password = strtok(NULL, " \t");
    if (ip == NULL || password == NULL || strtok(NULL, " \t") != NULL) return -1;
    if (inet_pton(AF_INET, ip, &v4) != 1 && inet_pton(AF_INET6, ip, &v6) != 1) return -1;
    gateway = &config->webirc_gateways[config->webirc_gateway_count];
    if (copy_value(gateway->ip, sizeof(gateway->ip), ip) != 0 || copy_value(gateway->password, sizeof(gateway->password), password) != 0) return -1;
    ++config->webirc_gateway_count;
    return 0;
}

static int add_dnsbl(ServerConfig *config, const char *value) {
    char copy[IRCD_CONFIG_LINE_MAX];
    char *name, *zone;
    DnsblZone *entry;
    if (config->dnsbl_count >= IRCD_MAX_DNSBLS || strlen(value) >= sizeof(copy)) return -1;
    (void)snprintf(copy, sizeof(copy), "%s", value);
    name = strtok(copy, " \t"); zone = strtok(NULL, " \t");
    if (name == NULL || zone == NULL || strtok(NULL, " \t") != NULL) return -1;
    entry = &config->dnsbls[config->dnsbl_count];
    if (copy_value(entry->name, sizeof(entry->name), name) != 0 || copy_value(entry->zone, sizeof(entry->zone), zone) != 0) return -1;
    ++config->dnsbl_count;
    return 0;
}

static int set_option(ServerConfig *config, const char *key, const char *value) {
    char *end = NULL;
    unsigned long number;
#define STRING_OPTION(name, field) if (strcmp(key, (name)) == 0) return copy_value(config->field, sizeof(config->field), value)
    if (strcmp(key, "webirc_gateway") == 0) return add_webirc_gateway(config, value);
    if (strcmp(key, "dnsbl") == 0) return add_dnsbl(config, value);
    STRING_OPTION("server_name", server_name); STRING_OPTION("network_name", network_name);
    STRING_OPTION("bind_address", bind_address); STRING_OPTION("port", port);
    STRING_OPTION("tls_port", tls_port); STRING_OPTION("tls_cert_file", tls_cert_file); STRING_OPTION("tls_key_file", tls_key_file);
    STRING_OPTION("geoip_city_db", geoip_city_db); STRING_OPTION("geoip_asn_db", geoip_asn_db);
    STRING_OPTION("server_password", server_password); STRING_OPTION("motd_file", motd_file); STRING_OPTION("rules_file", rules_file);
    STRING_OPTION("admin_location1", admin_location1); STRING_OPTION("admin_location2", admin_location2); STRING_OPTION("admin_email", admin_email);
    STRING_OPTION("operators_db", operators_db); STRING_OPTION("bans_db", bans_db); STRING_OPTION("nickserv_db", nickserv_db);
    STRING_OPTION("netadmin_name", netadmin_name); STRING_OPTION("netadmin_password_hash", netadmin_password_hash);
    STRING_OPTION("netadmin_hostmask", netadmin_hostmask); STRING_OPTION("netadmin_vhost", netadmin_vhost);
#undef STRING_OPTION
    errno = 0; number = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') return -1;
    if (strcmp(key, "max_clients") == 0) {
        if (number == 0UL || number > IRCD_HARD_MAX_CLIENTS) return -1;
        config->max_clients = (size_t)number; return 0;
    }
    if (strcmp(key, "dns_timeout_seconds") == 0) {
        if (number == 0UL || number > 300UL) return -1;
        config->dns_timeout_seconds = (unsigned int)number; return 0;
    }
    if (strcmp(key, "dnsbl_timeout_seconds") == 0) {
        if (number == 0UL || number > 300UL) return -1;
        config->dnsbl_timeout_seconds = (unsigned int)number; return 0;
    }
    return -1;
}

int runtime_config_load(ServerConfig *config, const char *path) {
    FILE *file; char line[IRCD_CONFIG_LINE_MAX]; unsigned long line_number = 0UL;
    if (config == NULL || path == NULL) return -1;
    file = fopen(path, "r"); if (file == NULL) return -1;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = trim(line), *equals, *key, *value;
        ++line_number; if (*text == '\0' || *text == '#') continue;
        equals = strchr(text, '=');
        if (equals == NULL) { fprintf(stderr, "%s:%lu: expected key=value\n", path, line_number); fclose(file); return -1; }
        *equals = '\0'; key = trim(text); value = trim(equals + 1);
        if (*key == '\0' || set_option(config, key, value) != 0) {
            fprintf(stderr, "%s:%lu: invalid option '%s'\n", path, line_number, key); fclose(file); return -1;
        }
    }
    if (ferror(file)) { fclose(file); return -1; }
    fclose(file); (void)copy_value(config->source_path, sizeof(config->source_path), path); return 0;
}
