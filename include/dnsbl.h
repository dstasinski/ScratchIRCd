#ifndef IRCD_DNSBL_H
#define IRCD_DNSBL_H

/**
 * @file dnsbl.h
 * @brief Asynchronous DNS blacklist queries for finalized client real_ip values.
 *
 * DNSBL lookups use a dedicated worker thread because libc resolver calls may
 * block. The event-loop thread exchanges fixed-size request/result records with
 * that worker through pipes and never performs blacklist DNS queries itself.
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

typedef struct DnsblZone {
    char name[IRCD_DNSBL_NAME_MAX + 1U]; /**< Human-readable list identifier. */
    char zone[IRCD_DNSBL_ZONE_MAX + 1U]; /**< DNS zone, e.g. dnsbl.example.net. */
} DnsblZone;

typedef struct DnsblRequest {
    uint64_t client_id;
    char ip[IRC_IP_MAX + 1U];
    size_t zone_count;
    DnsblZone zones[IRCD_MAX_DNSBLS];
} DnsblRequest;

typedef struct DnsblResult {
    uint64_t client_id;
    int completed; /**< Non-zero when the worker processed the request. */
    int listed;    /**< Non-zero when any configured DNSBL returned an address. */
    char name[IRCD_DNSBL_NAME_MAX + 1U];
    char zone[IRCD_DNSBL_ZONE_MAX + 1U];
} DnsblResult;

typedef struct DnsblResolver {
    pthread_t thread;
    int request_read_fd;
    int request_write_fd;
    int result_read_fd;
    int result_write_fd;
    int running;
    /** Stop after the currently executing lookup; do not drain queued work. */
    atomic_int stopping;
} DnsblResolver;

int dnsbl_resolver_init(DnsblResolver *resolver);
void dnsbl_resolver_destroy(DnsblResolver *resolver);
int dnsbl_resolver_submit(DnsblResolver *resolver, uint64_t client_id,
                          const char *ip, const DnsblZone *zones,
                          size_t zone_count);
int dnsbl_resolver_result_fd(const DnsblResolver *resolver);
int dnsbl_resolver_read_result(DnsblResolver *resolver, DnsblResult *result);

#endif /* IRCD_DNSBL_H */
