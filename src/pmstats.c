/** @file pmstats.c @brief Internal PMSTATS event feed. */
#include "server.h"

#include <stddef.h>

/* ScratchIRCd is deliberately single-server. Keep the active server only
 * while at least one PMSTATS subscriber exists. The client-free hook is used
 * because every ordinary disconnect converges on client_free() after the
 * disconnecting client has been removed from the live client array. Full
 * server teardown sets client_count to zero before freeing clients, so no
 * PMSTATS fanout is attempted while the server is being dismantled. */
static Server *pmstats_server = NULL;

static int pmstats_has_subscribers(const Server *server)
{
    size_t index;
    if (server == NULL) return 0;
    for (index = 0U; index < server->client_count; ++index) {
        const Client *client = server->clients[index];
        if (client != NULL && client->pmstats_enabled) return 1;
    }
    return 0;
}

static void pmstats_client_free(Client *client)
{
    Server *server = pmstats_server;
    if (server == NULL || client == NULL) return;

    if (client->registered && client->nick[0] != '\0')
        server_pmstats_disconnect(server, client);

    if (!pmstats_has_subscribers(server)) {
        pmstats_server = NULL;
        client_set_free_hook(NULL);
    }
}

void server_pmstats_set_enabled(Server *server, Client *client, int enabled)
{
    if (server == NULL || client == NULL) return;
    client->pmstats_enabled = enabled ? 1 : 0;
    if (client->pmstats_enabled) {
        pmstats_server = server;
        client_set_free_hook(pmstats_client_free);
    } else if (pmstats_server == server && !pmstats_has_subscribers(server)) {
        pmstats_server = NULL;
        client_set_free_hook(NULL);
    }
}

void server_pmstats_private_message(Server *server, const char *sender_nick,
                                    const char *receiver_nick)
{
    size_t index;
    if (server == NULL || sender_nick == NULL || receiver_nick == NULL) return;
    for (index = 0U; index < server->client_count; ++index) {
        Client *target = server->clients[index];
        if (target == NULL || !target->pmstats_enabled) continue;
        client_sendf(target, ":%s 801 %s :%s %s",
                     server->config.server_name, target->nick,
                     sender_nick, receiver_nick);
    }
}

void server_pmstats_nick_change(Server *server, const char *old_nick,
                                const char *new_nick)
{
    size_t index;
    if (server == NULL || old_nick == NULL || new_nick == NULL) return;
    for (index = 0U; index < server->client_count; ++index) {
        Client *target = server->clients[index];
        if (target == NULL || !target->pmstats_enabled) continue;
        client_sendf(target, ":%s 802 %s :%s %s",
                     server->config.server_name, target->nick,
                     old_nick, new_nick);
    }
}

void server_pmstats_disconnect(Server *server, const Client *disconnecting)
{
    size_t index;
    if (server == NULL || disconnecting == NULL ||
        !disconnecting->registered || disconnecting->nick[0] == '\0') return;
    for (index = 0U; index < server->client_count; ++index) {
        Client *target = server->clients[index];
        if (target == NULL || !target->pmstats_enabled) continue;
        client_sendf(target, ":%s 803 %s :%s",
                     server->config.server_name, target->nick,
                     disconnecting->nick);
    }
}
