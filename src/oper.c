/**
 * @file oper.c
 * @brief Operator privilege parsing, query, and formatting helpers.
 */

#include "oper.h"

#include <stdio.h>
#include <string.h>

/** One textual privilege name and its corresponding bit. */
typedef struct OperPermissionName {
    const char *name;
    OperPermissionSet bit;
} OperPermissionName;

static const OperPermissionName permission_names[] = {
    {"can_rehash",   OPER_PERMISSION_REHASH},
    {"can_die",      OPER_PERMISSION_DIE},
    {"can_restart",  OPER_PERMISSION_RESTART},
    {"helpop",       OPER_PERMISSION_HELPOP},
    {"can_wallops",  OPER_PERMISSION_WALLOPS},
    {"can_kill",     OPER_PERMISSION_KILL},
    {"can_kline",    OPER_PERMISSION_KLINE},
    {"can_unkline",  OPER_PERMISSION_UNKLINE},
    {"can_zline",    OPER_PERMISSION_ZLINE},
    {"get_host",     OPER_PERMISSION_GETHOST},
    {"can_override", OPER_PERMISSION_OVERRIDE},
    {"netadmin",     OPER_PERMISSION_NETADMIN}
};

int oper_permission_has(OperPermissionSet permissions, OperPermissionSet mask) {
    return (permissions & mask) != 0U;
}

static int lookup_permission(const char *name, OperPermissionSet *bit) {
    size_t i;

    for (i = 0U; i < sizeof(permission_names) / sizeof(permission_names[0]); ++i) {
        if (strcmp(name, permission_names[i].name) == 0) {
            *bit = permission_names[i].bit;
            return 0;
        }
    }
    return -1;
}

int oper_permissions_parse(const char *text, OperPermissionSet *permissions) {
    char copy[512];
    char *token;
    OperPermissionSet result = 0U;

    if (permissions == NULL) {
        return -1;
    }
    *permissions = 0U;

    if (text == NULL || *text == '\0') {
        return 0;
    }
    if (strlen(text) >= sizeof(copy)) {
        return -1;
    }

    (void)snprintf(copy, sizeof(copy), "%s", text);
    for (token = strtok(copy, ","); token != NULL; token = strtok(NULL, ",")) {
        OperPermissionSet bit;
        while (*token == ' ' || *token == '\t') {
            ++token;
        }
        if (strcmp(token, "oper") == 0) {
            continue;
        }
        if (lookup_permission(token, &bit) != 0) {
            return -1;
        }
        result |= bit;
    }

    *permissions = result;
    return 0;
}

size_t oper_permissions_format(OperPermissionSet permissions,
                               char *buffer, size_t buffer_size) {
    size_t i;
    size_t used = 0U;

    if (buffer == NULL || buffer_size == 0U) {
        return 0U;
    }
    buffer[0] = '\0';

    for (i = 0U; i < sizeof(permission_names) / sizeof(permission_names[0]); ++i) {
        int written;
        if ((permissions & permission_names[i].bit) == 0U) {
            continue;
        }
        written = snprintf(buffer + used, buffer_size - used, "%s%s",
                           used != 0U ? "," : "", permission_names[i].name);
        if (written < 0 || (size_t)written >= buffer_size - used) {
            break;
        }
        used += (size_t)written;
    }
    return used;
}
