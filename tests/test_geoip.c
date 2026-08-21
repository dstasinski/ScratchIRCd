#include "geoip.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    GeoIPContext context;
    ClientGeoIP geo;

    memset(&context, 0, sizeof(context));
    geoip_lookup(&context, "203.0.113.42", &geo);

    assert(strcmp(geo.status, "unavailable") == 0);
    assert(strcmp(geo.ip, "203.0.113.42") == 0);
    assert(geo.network[0] == '\0');
    assert(geo.source[0] == '\0');
    assert(geo.asn == 0U);

    puts("GeoIP unavailable-state test passed");
    return 0;
}
