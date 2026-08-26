/**
 * @file auth_limit.c
 * @brief Bounded fair-share limiter for client-triggered Argon2 work.
 *
 * Argon2 intentionally consumes substantial CPU and memory, and ScratchIRCd
 * currently performs password verification/hashing synchronously.  This
 * limiter therefore sits immediately before those expensive calls.  It uses
 * bounded, ephemeral per-real-IP state plus a global one-minute ceiling so
 * aliases, reconnects, or many source IPs cannot monopolize the event loop.
 */

#include "auth_limit.h"
#include "message_policy.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static NickServRegistrationThrottle *ip_slot(Server *server,
                                              const char *ip,
                                              time_t now) {
    NickServRegistrationThrottle *free_slot = NULL;
    size_t i;
    unsigned int window;

    if (server == NULL || ip == NULL || *ip == '\0') return NULL;
    window = server->config.argon2_window_seconds;
    if (window == 0U) return NULL;

    for (i = 0U; i < IRCD_NICKSERV_REGISTRATION_THROTTLE_SLOTS; ++i) {
        NickServRegistrationThrottle *slot = &server->argon2_throttles[i];
        if (slot->ip[0] != '\0' && strcmp(slot->ip, ip) == 0) {
            if (slot->window_start == 0 || now < slot->window_start ||
                now - slot->window_start >= (time_t)window) {
                slot->window_start = now;
                slot->count = 0U;
            }
            return slot;
        }
        if (slot->ip[0] == '\0' || slot->window_start == 0 ||
            now < slot->window_start ||
            now - slot->window_start >= (time_t)window) {
            if (free_slot == NULL) free_slot = slot;
        }
    }

    if (free_slot == NULL) return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    (void)snprintf(free_slot->ip, sizeof(free_slot->ip), "%s", ip);
    free_slot->window_start = now;
    return free_slot;
}

int auth_limit_consume(Server *server, Client *client, const char *operation) {
    NickServRegistrationThrottle *slot = NULL;
    unsigned int per_ip;
    unsigned int global;
    time_t now;

    if (server == NULL || client == NULL || client->real_ip[0] == '\0') return 0;
    per_ip = server->config.argon2_ops_per_ip;
    global = server->config.argon2_global_ops_per_minute;
    if (per_ip == 0U && global == 0U) return 1;

    now = time(NULL);

    if (global != 0U) {
        if (server->argon2_global_window_start == 0 ||
            now < server->argon2_global_window_start ||
            now - server->argon2_global_window_start >= 60) {
            server->argon2_global_window_start = now;
            server->argon2_global_count = 0U;
        }
        if (server->argon2_global_count >= global) {
            snotice_broadcast(server, SNOTICE_SECURITY | SNOTICE_FLOOD,
                              "Argon2 work globally throttled: nick=%s real_ip=%s operation=%s limit=%u/min",
                              client->nick[0] != '\0' ? client->nick : "*",
                              client->real_ip,
                              operation != NULL ? operation : "password",
                              global);
            return 0;
        }
    }

    if (per_ip != 0U) {
        slot = ip_slot(server, client->real_ip, now);
        if (slot == NULL || slot->count >= per_ip) {
            snotice_broadcast(server, SNOTICE_SECURITY | SNOTICE_FLOOD,
                              "Argon2 work throttled: nick=%s real_ip=%s operation=%s limit=%u/%us",
                              client->nick[0] != '\0' ? client->nick : "*",
                              client->real_ip,
                              operation != NULL ? operation : "password",
                              per_ip, server->config.argon2_window_seconds);
            return 0;
        }
    }

    if (slot != NULL) ++slot->count;
    if (global != 0U) ++server->argon2_global_count;
    return 1;
}
