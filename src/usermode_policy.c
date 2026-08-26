/**
 * @file usermode_policy.c
 * @brief Behavioral helpers for user modes +d and +x.
 *
 * Cloaking is presentation-only. It never changes Client.real_ip or
 * Client.real_host, so DNSBL/KLINE/ZLINE/GeoIP/operator inspection continue
 * to operate on the real security identity.
 */

#include "usermode_policy.h"
#include "cloak.h"
#include "modes.h"

#include <stdio.h>

int usermode_deaf_allows_text(const Client *recipient, const char *text) {
    if (recipient == NULL || text == NULL) return 0;
    if (!client_mode_has(recipient->modes, CLIENT_MODE_DEAF)) return 1;
    return text[0] != '\0' && strchr(IRCD_DEAF_COMMAND_PREFIXES, text[0]) != NULL;
}

void usermode_apply_cloak(const Server *server, Client *client) {
    if (server == NULL || client == NULL) return;

    client->modes = client_mode_add(client->modes, CLIENT_MODE_CLOAKED);
    cloak_refresh_display_host(&server->config, client);
}

void usermode_remove_cloak(Client *client) {
    const char *identity;

    if (client == NULL) return;

    client->modes = client_mode_remove(client->modes, CLIENT_MODE_CLOAKED);
    if (client_mode_has(client->modes, CLIENT_MODE_VHOST)) return;

    identity = client->real_host[0] != '\0' ? client->real_host : client->real_ip;
    (void)snprintf(client->display_host, sizeof(client->display_host), "%s", identity);
}
