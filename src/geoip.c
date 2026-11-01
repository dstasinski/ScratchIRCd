/**
 * @file geoip.c
 * @brief libmaxminddb-backed GeoIP/ASN enrichment for ScratchIRCd clients.
 */

#include "geoip.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static void copy_text(char *dest, size_t size, const char *src, size_t length) {
    if (dest == NULL || size == 0U) return;
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    if (length >= size) length = size - 1U;
    memcpy(dest, src, length);
    dest[length] = '\0';
}

static int get_utf8(MMDB_entry_s *entry, char *dest, size_t size,
                    const char *p1, const char *p2, const char *p3) {
    MMDB_entry_data_s data;
    int rc;

    memset(&data, 0, sizeof(data));
    if (p3 != NULL) rc = MMDB_get_value(entry, &data, p1, p2, p3, NULL);
    else if (p2 != NULL) rc = MMDB_get_value(entry, &data, p1, p2, NULL);
    else rc = MMDB_get_value(entry, &data, p1, NULL);

    if (rc != MMDB_SUCCESS || !data.has_data ||
        data.type != MMDB_DATA_TYPE_UTF8_STRING) return 0;

    copy_text(dest, size, data.utf8_string, data.data_size);
    return 1;
}

static int get_uint32(MMDB_entry_s *entry, uint32_t *value,
                      const char *p1, const char *p2) {
    MMDB_entry_data_s data;
    int rc;

    memset(&data, 0, sizeof(data));
    rc = p2 != NULL ? MMDB_get_value(entry, &data, p1, p2, NULL)
                    : MMDB_get_value(entry, &data, p1, NULL);
    if (rc != MMDB_SUCCESS || !data.has_data ||
        data.type != MMDB_DATA_TYPE_UINT32) return 0;
    *value = data.uint32;
    return 1;
}

/** Convert a guaranteed-numeric Client.real_ip into sockaddr storage. */
static int numeric_sockaddr(const char *ip, struct sockaddr_storage *storage) {
    struct sockaddr_in *v4;
    struct sockaddr_in6 *v6;

    memset(storage, 0, sizeof(*storage));
    v4 = (struct sockaddr_in *)storage;
    if (inet_pton(AF_INET, ip, &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        return 0;
    }

    memset(storage, 0, sizeof(*storage));
    v6 = (struct sockaddr_in6 *)storage;
    if (inet_pton(AF_INET6, ip, &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6;
        return 0;
    }
    return -1;
}

static void network_string(const char *ip, uint16_t raw_prefix,
                           char *buffer, size_t buffer_size) {
    struct in_addr v4;
    struct in6_addr v6;

    if (buffer == NULL || buffer_size == 0U) return;
    buffer[0] = '\0';

    if (inet_pton(AF_INET, ip, &v4) == 1) {
        uint16_t prefix = raw_prefix;
        uint32_t address;
        uint32_t mask;
        char text[INET_ADDRSTRLEN];

        /* IPv4 records in an IPv6 database report a 96-bit mapped offset. */
        if (prefix > 32U && prefix >= 96U) prefix = (uint16_t)(prefix - 96U);
        if (prefix > 32U) return;

        address = ntohl(v4.s_addr);
        mask = prefix == 0U ? 0U : (UINT32_C(0xffffffff) << (32U - prefix));
        address &= mask;
        v4.s_addr = htonl(address);
        if (inet_ntop(AF_INET, &v4, text, sizeof(text)) != NULL)
            (void)snprintf(buffer, buffer_size, "%s/%u", text, (unsigned)prefix);
        return;
    }

    if (inet_pton(AF_INET6, ip, &v6) == 1 && raw_prefix <= 128U) {
        unsigned int full = raw_prefix / 8U;
        unsigned int remainder = raw_prefix % 8U;
        unsigned int i;
        char text[INET6_ADDRSTRLEN];

        if (full < 16U) {
            if (remainder != 0U) {
                v6.s6_addr[full] &= (unsigned char)(0xffU << (8U - remainder));
                ++full;
            }
            for (i = full; i < 16U; ++i) v6.s6_addr[i] = 0U;
        }
        if (inet_ntop(AF_INET6, &v6, text, sizeof(text)) != NULL)
            (void)snprintf(buffer, buffer_size, "%s/%u", text,
                           (unsigned)raw_prefix);
    }
}

static void open_database(MMDB_s *db, int *opened, const char *path) {
    int rc;
    if (path == NULL || *path == '\0') return;
    rc = MMDB_open(path, MMDB_MODE_MMAP, db);
    if (rc == MMDB_SUCCESS) *opened = 1;
    else fprintf(stderr, "GeoIP database unavailable (%s): %s\n",
                 path, MMDB_strerror(rc));
}

int geoip_init_dual(GeoIPContext *context, const char *city_path,
                    const char *asn_path, const char *ip2_city_path,
                    const char *ip2_asn_path, const char *preferred_provider) {
    if (context == NULL) return -1;
    memset(context, 0, sizeof(*context));
    context->prefer_ip2location = preferred_provider != NULL &&
                                 strcmp(preferred_provider, "ip2location") == 0;
    open_database(&context->city, &context->city_open, city_path);
    open_database(&context->asn, &context->asn_open, asn_path);
    open_database(&context->ip2location_city, &context->ip2location_city_open, ip2_city_path);
    open_database(&context->ip2location_asn, &context->ip2location_asn_open, ip2_asn_path);
    return 0;
}

int geoip_init(GeoIPContext *context, const char *city_path, const char *asn_path) {
    return geoip_init_dual(context, city_path, asn_path, NULL, NULL, "maxmind");
}

void geoip_destroy(GeoIPContext *context) {
    if (context == NULL) return;
    if (context->city_open) MMDB_close(&context->city);
    if (context->asn_open) MMDB_close(&context->asn);
    if (context->ip2location_city_open) MMDB_close(&context->ip2location_city);
    if (context->ip2location_asn_open) MMDB_close(&context->ip2location_asn);
    memset(context, 0, sizeof(*context));
}

static void lookup_city(const MMDB_s *db, const char *ip,
                        const struct sockaddr_storage *address,
                        ClientGeoIP *out, int *found, int *error) {
    MMDB_lookup_result_s entry;
    int rc = MMDB_SUCCESS;
    if (db == NULL) return;
    entry = MMDB_lookup_sockaddr(db, (const struct sockaddr *)address, &rc);
    if (rc != MMDB_SUCCESS) { *error = 1; return; }
    if (!entry.found_entry) return;
    *found = 1;
    (void)get_utf8(&entry.entry, out->continent_code, sizeof(out->continent_code), "continent", "code", NULL);
    (void)get_utf8(&entry.entry, out->country_code, sizeof(out->country_code), "country", "iso_code", NULL);
    (void)get_utf8(&entry.entry, out->country_name, sizeof(out->country_name), "country", "names", "en");
    (void)get_utf8(&entry.entry, out->region_code, sizeof(out->region_code), "subdivisions", "0", "iso_code");
    {
        MMDB_entry_data_s data;
        memset(&data, 0, sizeof(data));
        if (MMDB_get_value(&entry.entry, &data, "subdivisions", "0", "names", "en", NULL) == MMDB_SUCCESS &&
            data.has_data && data.type == MMDB_DATA_TYPE_UTF8_STRING)
            copy_text(out->region_name, sizeof(out->region_name), data.utf8_string, data.data_size);
    }
    (void)get_utf8(&entry.entry, out->city, sizeof(out->city), "city", "names", "en");
    network_string(ip, entry.netmask, out->network, sizeof(out->network));
}

static void lookup_asn(const MMDB_s *db, const char *ip,
                       const struct sockaddr_storage *address,
                       ClientGeoIP *out, int *found, int *error) {
    MMDB_lookup_result_s entry;
    int rc = MMDB_SUCCESS;
    if (db == NULL) return;
    entry = MMDB_lookup_sockaddr(db, (const struct sockaddr *)address, &rc);
    if (rc != MMDB_SUCCESS) { *error = 1; return; }
    if (!entry.found_entry) return;
    *found = 1;
    (void)get_uint32(&entry.entry, &out->asn, "autonomous_system_number", NULL);
    (void)get_utf8(&entry.entry, out->organization, sizeof(out->organization),
                   "autonomous_system_organization", NULL, NULL);
    network_string(ip, entry.netmask, out->network, sizeof(out->network));
}

static void choose_text(char *dest, size_t size, char *source, size_t source_size,
                        const char *primary, const char *fallback,
                        const char *primary_name, const char *fallback_name) {
    const char *chosen = primary[0] != '\0' ? primary : fallback;
    if (chosen[0] == '\0') return;
    (void)snprintf(dest, size, "%s", chosen);
    (void)snprintf(source, source_size, "%s",
                   primary[0] != '\0' ? primary_name : fallback_name);
}

void geoip_lookup(const GeoIPContext *context, const char *ip, ClientGeoIP *result) {
    struct sockaddr_storage address;
    ClientGeoIP maxmind, ip2, *primary, *fallback;
    const char *primary_name, *fallback_name;
    int city_found = 0, asn_found = 0, ip2_city_found = 0, ip2_asn_found = 0;
    int error = 0;
    if (result == NULL) return;
    memset(result, 0, sizeof(*result));
    if (ip != NULL) (void)snprintf(result->ip, sizeof(result->ip), "%s", ip);
    if (context == NULL ||
        (!context->city_open && !context->asn_open &&
         !context->ip2location_city_open && !context->ip2location_asn_open) ||
        ip == NULL || *ip == '\0') {
        (void)snprintf(result->status, sizeof(result->status), "unavailable");
        return;
    }
    if (numeric_sockaddr(ip, &address) != 0) {
        (void)snprintf(result->status, sizeof(result->status), "error");
        return;
    }
    memset(&maxmind, 0, sizeof(maxmind));
    memset(&ip2, 0, sizeof(ip2));
    if (context->city_open)
        lookup_city(&context->city, ip, &address, &maxmind, &city_found, &error);
    if (context->asn_open)
        lookup_asn(&context->asn, ip, &address, &maxmind, &asn_found, &error);
    if (context->ip2location_city_open)
        lookup_city(&context->ip2location_city, ip, &address, &ip2, &ip2_city_found, &error);
    if (context->ip2location_asn_open)
        lookup_asn(&context->ip2location_asn, ip, &address, &ip2, &ip2_asn_found, &error);
    primary = context->prefer_ip2location ? &ip2 : &maxmind;
    fallback = context->prefer_ip2location ? &maxmind : &ip2;
    primary_name = context->prefer_ip2location ? "IP2Location" : "MaxMind";
    fallback_name = context->prefer_ip2location ? "MaxMind" : "IP2Location";
#define CHOOSE(field, provenance) \
    choose_text(result->field, sizeof(result->field), result->provenance, \
                sizeof(result->provenance), primary->field, fallback->field, \
                primary_name, fallback_name)
    CHOOSE(country_code, country_source);
    CHOOSE(country_name, country_source);
    CHOOSE(region_code, region_source);
    CHOOSE(region_name, region_source);
    CHOOSE(city, city_source);
    CHOOSE(organization, organization_source);
#undef CHOOSE
    choose_text(result->continent_code, sizeof(result->continent_code),
                result->source, sizeof(result->source), primary->continent_code,
                fallback->continent_code, primary_name, fallback_name);
    if (primary->asn != 0U) {
        result->asn = primary->asn;
        (void)snprintf(result->asn_source, sizeof(result->asn_source), "%s", primary_name);
    } else if (fallback->asn != 0U) {
        result->asn = fallback->asn;
        (void)snprintf(result->asn_source, sizeof(result->asn_source), "%s", fallback_name);
    }
    (void)snprintf(result->network, sizeof(result->network), "%s",
                   primary->network[0] ? primary->network : fallback->network);
    if (city_found || asn_found || ip2_city_found || ip2_asn_found) {
        (void)snprintf(result->status, sizeof(result->status), "ok");
        (void)snprintf(result->source, sizeof(result->source), "%s",
                       (city_found || asn_found) && (ip2_city_found || ip2_asn_found)
                           ? (context->prefer_ip2location ? "IP2Location+MaxMind" : "MaxMind+IP2Location")
                           : (ip2_city_found || ip2_asn_found) ? "IP2Location" : "MaxMind");
    } else
        (void)snprintf(result->status, sizeof(result->status), "%s",
                       error ? "error" : "not_found");
}
