#ifndef IRCD_GEOIP_H
#define IRCD_GEOIP_H

/**
 * @file geoip.h
 * @brief MaxMind GeoLite2 City/ASN enrichment for finalized client IPs.
 *
 * GeoIP data is enrichment state, not part of ScratchIRCd's three-field
 * host identity model.  Lookups always use Client.real_ip after direct/WebIRC
 * identity has been finalized and copy all values out of the MMDB mapping.
 */

#include <stdint.h>
#include <maxminddb.h>

#include "config.h"

/** GeoIP information retained on each Client for later policy/features. */
typedef struct ClientGeoIP {
    char status[IRCD_GEOIP_STATUS_MAX + 1U];
    char ip[IRC_IP_MAX + 1U];
    char network[IRCD_GEOIP_NETWORK_MAX + 1U];
    char source[IRCD_GEOIP_SOURCE_MAX + 1U];
    char continent_code[IRCD_GEOIP_CODE_MAX + 1U];
    char country_code[IRCD_GEOIP_CODE_MAX + 1U];
    char country_name[IRCD_GEOIP_NAME_MAX + 1U];
    char region_code[IRCD_GEOIP_REGION_CODE_MAX + 1U];
    char region_name[IRCD_GEOIP_NAME_MAX + 1U];
    char city[IRCD_GEOIP_NAME_MAX + 1U];
    uint32_t asn;
    char organization[IRCD_GEOIP_ORG_MAX + 1U];
} ClientGeoIP;

/** Process-level memory-mapped MaxMind database handles. */
typedef struct GeoIPContext {
    MMDB_s city;
    MMDB_s asn;
    int city_open;
    int asn_open;
} GeoIPContext;

/** Open whichever configured databases are available. Missing files are non-fatal. */
int geoip_init(GeoIPContext *context, const char *city_path, const char *asn_path);

/** Close all opened MMDB mappings. */
void geoip_destroy(GeoIPContext *context);

/** Populate one ClientGeoIP record from a finalized numeric IP address. */
void geoip_lookup(const GeoIPContext *context, const char *ip, ClientGeoIP *result);

#endif /* IRCD_GEOIP_H */
