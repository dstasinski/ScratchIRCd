#include "geoip.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    GeoIPContext context;
    ClientGeoIP geo;

    memset(&context, 0, sizeof(context));
    geoip_lookup(&context, "203.0.113.42", &geo);

    assert(strcmp(geo.status, "unavailable") == 0);
    assert(strcmp(geo.ip, "203.0.113.42") == 0);
    assert(geo.network[0] == '\0');
    assert(geo.source[0] == '\0');
    assert(geo.asn == 0U);

    /* Optional real-database compatibility check. The ordinary unit test
     * remains fixture-free; supply City and ASN MMDB paths to validate an
     * installed IP2Location LITE or MaxMind GeoLite2 database pair. */
    if (argc == 3) {
        assert(geoip_init(&context, argv[1], argv[2]) == 0);
        assert(context.city_open && context.asn_open);
        geoip_lookup(&context, "8.8.8.8", &geo);
        assert(strcmp(geo.status, "ok") == 0);
        assert(geo.country_code[0] != '\0');
        assert(geo.country_name[0] != '\0');
        assert(geo.asn != 0U);
        assert(geo.organization[0] != '\0');
        assert(geo.source[0] != '\0');
        geoip_destroy(&context);
        puts("GeoIP City/ASN MMDB compatibility test passed");
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [city.mmdb asn.mmdb]\n", argv[0]);
        return 2;
    }

    /* Both provider orders must work with no configured MMDB files. */
    assert(geoip_init_dual(&context, NULL, NULL, NULL, NULL, "maxmind") == 0);
    assert(context.prefer_ip2location == 0);
    geoip_lookup(&context, "203.0.113.42", &geo);
    assert(strcmp(geo.status, "unavailable") == 0);
    geoip_destroy(&context);
    assert(geoip_init_dual(&context, NULL, NULL, NULL, NULL, "ip2location") == 0);
    assert(context.prefer_ip2location == 1);
    geoip_lookup(&context, "203.0.113.42", &geo);
    assert(strcmp(geo.status, "unavailable") == 0);
    geoip_destroy(&context);

    puts("GeoIP unavailable-state test passed");
    return 0;
}
