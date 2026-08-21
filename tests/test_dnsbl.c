#include "dnsbl.h"

#include <assert.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(void) {
    DnsblResolver resolver;
    DnsblResult result;
    struct timespec pause = {0, 10000000L};
    int attempts;

    assert(dnsbl_resolver_init(&resolver) == 0);

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
