/**
 * @file runtime_config.c
 * @brief Runtime configuration loading for ScratchIRCd.
 *
 * The parser is intentionally small and strict. It accepts key=value pairs,
 * trims surrounding whitespace, ignores comments/blank lines, and rejects
 * unknown or invalid keys. This gives the project a stable configuration
 * boundary before TLS, WebIRC, services, and database settings are added.
 */

#include "runtime_config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *text) {
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    if (*text == '\0') {
        return text;
    }

    end = text + strlen(text) - 1U;
    while (end > text && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return text;
}

static int copy_value(char *dest, size_t size, const char *value) {
    int written = snprintf(dest, size, "%s", value);
    return written >= 0 && (size_t)written < size ? 0 : -1;
}

void runtime_config_defaults(ServerConfig *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    (void)copy_value(config->server_name, sizeof(config->server_name), IRCD_DEFAULT_SERVER_NAME);
    (void)copy_value(config->network_name, sizeof(config->network_name), IRCD_DEFAULT_NETWORK_NAME);
    (void)copy_value(config->bind_address, sizeof(config->bind_address), IRCD_DEFAULT_BIND_ADDRESS);
    (void)copy_value(config->port, sizeof(config->port), IRCD_DEFAULT_PORT);
    (void)copy_value(config->motd_file, sizeof(config->motd_file), IRCD_DEFAULT_MOTD_FILE);
    (void)copy_value(config->rules_file, sizeof(config->rules_file), IRCD_DEFAULT_RULES_FILE);
    config->max_clients = IRCD_DEFAULT_MAX_CLIENTS;
    config->dns_timeout_seconds = IRCD_DEFAULT_DNS_TIMEOUT_SECONDS;
}

static int set_option(ServerConfig *config, const char *key, const char *value) {
    char *end = NULL;
    unsigned long number;

#define STRING_OPTION(name, field) \
    if (strcmp(key, (name)) == 0) { \
        return copy_value(config->field, sizeof(config->field), value); \
    }

    STRING_OPTION("server_name", server_name)
    STRING_OPTION("network_name", network_name)
    STRING_OPTION("bind_address", bind_address)
    STRING_OPTION("port", port)
    STRING_OPTION("server_password", server_password)
    STRING_OPTION("motd_file", motd_file)
    STRING_OPTION("rules_file", rules_file)
    STRING_OPTION("admin_location1", admin_location1)
    STRING_OPTION("admin_location2", admin_location2)
    STRING_OPTION("admin_email", admin_email)

#undef STRING_OPTION

    errno = 0;
    number = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        return -1;
    }

    if (strcmp(key, "max_clients") == 0) {
        if (number == 0UL || number > IRCD_HARD_MAX_CLIENTS) {
            return -1;
        }
        config->max_clients = (size_t)number;
        return 0;
    }
    if (strcmp(key, "dns_timeout_seconds") == 0) {
        if (number == 0UL || number > 300UL) {
            return -1;
        }
        config->dns_timeout_seconds = (unsigned int)number;
        return 0;
    }

    return -1;
}

int runtime_config_load(ServerConfig *config, const char *path) {
    FILE *file;
    char line[IRCD_CONFIG_LINE_MAX];
    unsigned long line_number = 0UL;

    if (config == NULL || path == NULL) {
        return -1;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *text;
        char *equals;
        char *key;
        char *value;

        ++line_number;
        text = trim(line);
        if (*text == '\0' || *text == '#') {
            continue;
        }

        equals = strchr(text, '=');
        if (equals == NULL) {
            fprintf(stderr, "%s:%lu: expected key=value\n", path, line_number);
            fclose(file);
            return -1;
        }

        *equals = '\0';
        key = trim(text);
        value = trim(equals + 1);
        if (*key == '\0' || set_option(config, key, value) != 0) {
            fprintf(stderr, "%s:%lu: invalid option '%s'\n", path, line_number, key);
            fclose(file);
            return -1;
        }
    }

    if (ferror(file)) {
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}
