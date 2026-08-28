#ifndef IRCD_DNS_H
#define IRCD_DNS_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include "config.h"

typedef struct DnsRequest {
    uint64_t client_id;
    int address_family;
    char ip[IRC_IP_MAX + 1U];
} DnsRequest;

typedef struct DnsResult {
    uint64_t client_id;
    int verified;
    char queried_ip[IRC_IP_MAX + 1U];
    char resolved_host[IRC_HOST_MAX + 1U];
} DnsResult;

typedef struct DnsResolver {
    pthread_t thread;
    int request_read_fd;
    int request_write_fd;
    int result_read_fd;
    int result_write_fd;
    int running;
    /** Stop after the currently executing lookup; do not drain queued work. */
    atomic_int stopping;
} DnsResolver;

int dns_resolver_init(DnsResolver *resolver);
void dns_resolver_destroy(DnsResolver *resolver);
int dns_resolver_submit(DnsResolver *resolver, uint64_t client_id,
                        int address_family, const char *ip);
int dns_resolver_result_fd(const DnsResolver *resolver);
int dns_resolver_read_result(DnsResolver *resolver, DnsResult *result);

#endif
