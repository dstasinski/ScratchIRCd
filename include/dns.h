#ifndef IRCD_DNS_H
#define IRCD_DNS_H

#include <pthread.h>
#include <stdint.h>

#include "config.h"

/** Request passed from the IRC event loop to the DNS worker thread. */
typedef struct DnsRequest {
    uint64_t client_id;                  /**< Stable connection identifier. */
    int address_family;                  /**< AF_INET or AF_INET6. */
    char ip[IRC_IP_MAX + 1U];            /**< Numeric real client address. */
} DnsRequest;

/**
 * Result returned by the DNS worker to the IRC event loop.
 *
 * PTR and forward lookup details remain internal to the resolver worker. The
 * event loop receives only the final FCrDNS decision and verified hostname.
 */
typedef struct DnsResult {
    uint64_t client_id;                  /**< Connection this answer belongs to. */
    int verified;                        /**< Non-zero when FCrDNS succeeded. */
    char resolved_host[IRC_HOST_MAX + 1U]; /**< Verified hostname, else empty. */
} DnsResult;

/**
 * Dedicated asynchronous DNS resolver.
 *
 * libc resolver functions may block. ScratchIRCd therefore executes them on
 * this worker thread and exchanges fixed-size request/result records through
 * pipes. The result pipe is included in the server's normal poll() set, so DNS
 * never blocks socket processing.
 */
typedef struct DnsResolver {
    pthread_t thread;
    int request_read_fd;
    int request_write_fd;
    int result_read_fd;
    int result_write_fd;
    int running;
} DnsResolver;

int dns_resolver_init(DnsResolver *resolver);
void dns_resolver_destroy(DnsResolver *resolver);
int dns_resolver_submit(DnsResolver *resolver, uint64_t client_id,
                        int address_family, const char *ip);
int dns_resolver_result_fd(const DnsResolver *resolver);
int dns_resolver_read_result(DnsResolver *resolver, DnsResult *result);

#endif /* IRCD_DNS_H */
