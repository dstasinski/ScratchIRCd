/**
 * @file usermode_policy.c
 * @brief Behavioral helpers for user modes +d and +x.
 *
 * Cloaking is presentation-only. It never changes Client.real_ip or
 * Client.real_host, so DNSBL/KLINE/ZLINE/GeoIP/operator inspection continue
 * to operate on the real security identity.
 */

#include "usermode_policy.h"
#include "config.h"
#include "modes.h"

#include <openssl/sha.h>
#include <stdio.h>
#include <string.h>

int usermode_deaf_allows_text(const Client *recipient, const char *text) {
    if (recipient == NULL || text == NULL) return 0;
    if (!client_mode_has(recipient->modes, CLIENT_MODE_DEAF)) return 1;
    return text[0] != '\0' && strchr(IRCD_DEAF_COMMAND_PREFIXES, text[0]) != NULL;
}

void usermode_apply_cloak(const Server *server, Client *client) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    char input[IRC_HOST_MAX + IRCD_NETWORK_NAME_MAX + 4U];
    char cloak[IRC_HOST_MAX + 1U];
    const char *identity;
    size_t i;
    size_t used;

    if (server == NULL || client == NULL) return;
    identity = client->real_host[0] != '\0' ? client->real_host : client->real_ip;
    (void)snprintf(input, sizeof(input), "%s|%s",
                   server->config.network_name, identity);
    SHA256((const unsigned char *)input, strlen(input), digest);

    used = (size_t)snprintf(cloak, sizeof(cloak), "cloak-");
    for (i = 0U; i < IRCD_CLOAK_HEX_BYTES && used + 2U < sizeof(cloak); ++i) {
        int written = snprintf(cloak + used, sizeof(cloak) - used,
                               "%02x", digest[i]);
        if (written != 2) break;
        used += 2U;
    }
    cloak[sizeof(cloak) - 1U] = '\0';

    (void)snprintf(client->display_host, sizeof(client->display_host), "%s", cloak);
    /* +x and +t are alternate display-host sources; the newest choice wins. */
    client->modes = client_mode_remove(client->modes, CLIENT_MODE_VHOST);
    client->modes = client_mode_add(client->modes, CLIENT_MODE_CLOAKED);
}

void usermode_remove_cloak(Client *client) {
    const char *identity;
    if (client == NULL) return;
    client->modes = client_mode_remove(client->modes, CLIENT_MODE_CLOAKED);
    if (client_mode_has(client->modes, CLIENT_MODE_VHOST)) return;
    identity = client->real_host[0] != '\0' ? client->real_host : client->real_ip;
    (void)snprintf(client->display_host, sizeof(client->display_host), "%s", identity);
}
