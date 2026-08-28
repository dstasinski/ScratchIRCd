#include "dnsbl.h"

#include <assert.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(void) {
    DnsblResolver resolver;
    DnsblResolver inactive;
    DnsblResult result;
    struct timespec pause = {0, 10000000L};
    int attempts;

    /* server_init() failure cleanup may destroy a resolver that was never
     * initialized. That path must not touch the uninitialized atomic flag. */
    memset(&inactive, 0, sizeof(inactive));
    inactive.request_read_fd = inactive.request_write_fd = -1;
    inactive.result_read_fd = inactive.result_write_fd = -1;
    dnsbl_resolver_destroy(&inactive);

    assert(dnsbl_resolver_init(&resolver) == 0);

    /* Once teardown has begun no additional work may enter the request pipe. */
    atomic_store_explicit(&resolver.stopping, 1, memory_order_relaxed);
    assert(dnsbl_resolver_submit(&resolver, 41U, "203.0.113.8", NULL, 0U) == -1);
    atomic_store_explicit(&resolver.stopping, 0, memory_order_relaxed);

    /* Zero zones performs no network lookup but exercises the worker/pipes. */
    assert(dnsbl_resolver_submit(&resolver, 42U, "203.0.113.9", NULL, 0U) == 0);
    memset(&result, 0, sizeof(result));
    for (attempts = 0; attempts < 100; ++attempts) {
        int rc = dnsbl_resolver_read_result(&resolver, &result);
        if (rc == 1) break;
        assert(rc == 0);
        (void)nanosleep(&pause, NULL);
    }

    assert(result.client_id == 42U);
    assert(result.completed != 0);
    assert(result.listed == 0);
    dnsbl_resolver_destroy(&resolver);
    return 0;
}
