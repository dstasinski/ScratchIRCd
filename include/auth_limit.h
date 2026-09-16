#ifndef IRCD_AUTH_LIMIT_H
#define IRCD_AUTH_LIMIT_H

#include "oper.h"
#include "server.h"

#include <argon2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/**
 * Perform normal-cost Argon2id work when no real password hash is available.
 *
 * The result is deliberately discarded. This keeps unavailable identities
 * from creating a cheap authentication path or an obvious timing oracle.
 * Call auth_limit_consume() before invoking this function.
 */
static inline void auth_password_dummy_work(const char *password) {
    static const uint8_t salt[IRCD_ARGON2_SALT_BYTES] = {0};
    uint8_t output[IRCD_ARGON2_HASH_BYTES];

    if (password == NULL) return;

    (void)argon2id_hash_raw(IRCD_ARGON2_TIME_COST,
                            IRCD_ARGON2_MEMORY_COST_KIB,
                            IRCD_ARGON2_PARALLELISM,
                            password, strlen(password),
                            salt, sizeof(salt),
                            output, sizeof(output));
}

/**
 * Consume one client-triggered password-hashing/verification work unit.
 * Returns non-zero when the expensive Argon2 operation may proceed.
 * State is bounded, memory-only, and keyed by the client's real IP.
 */
static inline int auth_limit_consume(Server *server, Client *client,
                                     const char *operation) {
    NickServRegistrationThrottle *slot = NULL;
    NickServRegistrationThrottle *free_slot = NULL;
    unsigned int per_ip;
    unsigned int global;
    unsigned int burst;
    unsigned int window;
    time_t now;
    size_t i;

    if (server == NULL || client == NULL || client->real_ip[0] == '\0') return 0;
    per_ip = server->config.argon2_ops_per_ip;
    global = server->config.argon2_global_ops_per_minute;
    burst = server->config.argon2_global_burst_per_second;
    window = server->config.argon2_window_seconds;
    if (per_ip == 0U && global == 0U && burst == 0U) return 1;

    now = time(NULL);

    if (burst != 0U) {
        if (server->argon2_burst_window_start == 0 ||
            now < server->argon2_burst_window_start ||
            now - server->argon2_burst_window_start >= 1) {
            server->argon2_burst_window_start = now;
            server->argon2_burst_count = 0U;
        }
        if (server->argon2_burst_count >= burst) {
            snotice_broadcast(server, SNOTICE_SECURITY | SNOTICE_FLOOD,
                              "Argon2 work burst throttled: nick=%s real_ip=%s operation=%s limit=%u/sec",
                              client->nick[0] != '\0' ? client->nick : "*",
                              client->real_ip,
                              operation != NULL ? operation : "password",
                              burst);
            return 0;
        }
    }

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
        for (i = 0U; i < IRCD_NICKSERV_REGISTRATION_THROTTLE_SLOTS; ++i) {
            NickServRegistrationThrottle *candidate = &server->argon2_throttles[i];
            if (candidate->ip[0] != '\0' && strcmp(candidate->ip, client->real_ip) == 0) {
                if (candidate->window_start == 0 || now < candidate->window_start ||
                    now - candidate->window_start >= (time_t)window) {
                    candidate->window_start = now;
                    candidate->count = 0U;
                }
                slot = candidate;
                break;
            }
            if (candidate->ip[0] == '\0' || candidate->window_start == 0 ||
                now < candidate->window_start ||
                now - candidate->window_start >= (time_t)window) {
                if (free_slot == NULL) free_slot = candidate;
            }
        }
        if (slot == NULL && free_slot != NULL) {
            memset(free_slot, 0, sizeof(*free_slot));
            (void)snprintf(free_slot->ip, sizeof(free_slot->ip), "%s", client->real_ip);
            free_slot->window_start = now;
            slot = free_slot;
        }
        if (slot == NULL || slot->count >= per_ip) {
            snotice_broadcast(server, SNOTICE_SECURITY | SNOTICE_FLOOD,
                              "Argon2 work throttled: nick=%s real_ip=%s operation=%s limit=%u/%us",
                              client->nick[0] != '\0' ? client->nick : "*",
                              client->real_ip,
                              operation != NULL ? operation : "password",
                              per_ip, window);
            return 0;
        }
    }

    if (slot != NULL) ++slot->count;
    if (global != 0U) ++server->argon2_global_count;
    if (burst != 0U) ++server->argon2_burst_count;
    return 1;
}

#endif /* IRCD_AUTH_LIMIT_H */
