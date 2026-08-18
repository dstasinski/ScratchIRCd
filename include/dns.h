#ifndef IRCD_DNS_H
#define IRCD_DNS_H

#include <pthread.h>
#include <stdint.h>

#include "config.h"

/** Request passed from the IRC event loop to the DNS worker thread. */
typedef struct DnsRequest {
    uint64_t client_id;                  /**< Stable connection identifier. */
    int address_family;                  /**< AF_INET or AF_INET6. */
    char ip[IRC_IP_MAX + 1U];            /**< Numeric client address. */
} DnsRequest;

/** Result returned by the DNS worker to the IRC event loop. */
typedef struct DnsResult {
    uint64_t client_id;                  /**< Connection this answer belongs to. */
    int verified;                        /**< Non-zero when FCrDNS succeeded. */
    char reverse_host[IRC_HOST_MAX + 1U]; /**< PTR hostname, if one exists. */
    char forward_host[IRC_HOST_MAX + 1U]; /**< FCrDNS-confirmed hostname. */
} DnsResult;

/**
 * Dedicated asynchronous DNS resolver.
 *
 * libc resolver functions may block.  ScratchIRCd therefore executes them on
 * this worker thread and exchanges fixed-size request/result records through
 * pipes.  The result pipe is included in the server's normal poll() set, so
 * DNS never blocks socket processing.
 */
typedef struct DnsResolver {
    pthread_t thread;                    /**< Resolver worker thread. */
    int request_read_fd;                 /**< Worker side of request pipe. */
    int request_write_fd;                /**< Event-loop side of request pipe. */
    int result_read_fd;                  /**< Event-loop side of result pipe. */
    int result_write_fd;                 /**< Worker side of result pipe. */
    int running;                         /**< Non-zero after successful init. */
} DnsResolver;

/** Initialize pipes and launch the resolver worker thread. */
int dns_resolver_init(DnsResolver *resolver);

/** Stop the worker and close all resolver file descriptors. */
void dns_resolver_destroy(DnsResolver *resolver);

/** Queue an address for reverse lookup and forward verification. */
int dns_resolver_submit(DnsResolver *resolver, uint64_t client_id,
                        int address_family, const char *ip);

/** File descriptor the event loop should poll for completed DNS results. */
int dns_resolver_result_fd(const DnsResolver *resolver);

/** Read one completed result; returns 1, 0 for no result, or -1 on error. */
int dns_resolver_read_result(DnsResolver *resolver, DnsResult *result);

#endif /* IRCD_DNS_H */
