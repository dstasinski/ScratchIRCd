/**
 * @file geoip.c
 * @brief libmaxminddb-backed GeoIP/ASN enrichment for ScratchIRCd clients.
 */

#include "geoip.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

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

int geoip_init(GeoIPContext *context, const char *city_path, const char *asn_path) {
    int city_rc = MMDB_FILE_OPEN_ERROR;
    int asn_rc = MMDB_FILE_OPEN_ERROR;

    if (context == NULL) return -1;
    memset(context, 0, sizeof(*context));

    if (city_path != NULL && city_path[0] != '\0') {
        city_rc = MMDB_open(city_path, MMDB_MODE_MMAP, &context->city);
        if (city_rc == MMDB_SUCCESS) context->city_open = 1;
        else if (city_rc != MMDB_FILE_OPEN_ERROR)
            fprintf(stderr, "GeoIP City database unavailable (%s): %s\n",
                    city_path, MMDB_strerror(city_rc));
    }

    if (asn_path != NULL && asn_path[0] != '\0') {
        asn_rc = MMDB_open(asn_path, MMDB_MODE_MMAP, &context->asn);
        if (asn_rc == MMDB_SUCCESS) context->asn_open = 1;
        else if (asn_rc != MMDB_FILE_OPEN_ERROR)
            fprintf(stderr, "GeoIP ASN database unavailable (%s): %s\n",
                    asn_path, MMDB_strerror(asn_rc));
    }

    /* Missing optional databases are deliberately non-fatal. */
    return 0;
}

void geoip_destroy(GeoIPContext *context) {
    if (context == NULL) return;
    if (context->city_open) MMDB_close(&context->city);
    if (context->asn_open) MMDB_close(&context->asn);
    memset(context, 0, sizeof(*context));
}

void geoip_lookup(const GeoIPContext *context, const char *ip, ClientGeoIP *result) {
    MMDB_lookup_result_s city_result;
    MMDB_lookup_result_s asn_result;
    int city_found = 0;
    int asn_found = 0;
    int error = 0;

    if (result == NULL) return;
    memset(result, 0, sizeof(*result));
    if (ip != NULL) (void)snprintf(result->ip, sizeof(result->ip), "%s", ip);

    if (context == NULL || (!context->city_open && !context->asn_open) ||
        ip == NULL || ip[0] == '\0') {
        (void)snprintf(result->status, sizeof(result->status), "unavailable");
        return;
    }

    memset(&city_result, 0, sizeof(city_result));
    memset(&asn_result, 0, sizeof(asn_result));

    if (context->city_open) {
        int gai_error = 0;
        int mmdb_error = MMDB_SUCCESS;
        city_result = MMDB_lookup_string(&context->city, ip, &gai_error, &mmdb_error);
        if (gai_error != 0 || mmdb_error != MMDB_SUCCESS) {
            error = 1;
        } else if (city_result.found_entry) {
            city_found = 1;
            (void)get_utf8(&city_result.entry, result->continent_code,
                           sizeof(result->continent_code), "continent", "code", NULL);
            (void)get_utf8(&city_result.entry, result->country_code,
                           sizeof(result->country_code), "country", "iso_code", NULL);
            (void)get_utf8(&city_result.entry, result->country_name,
                           sizeof(result->country_name), "country", "names", "en");
            (void)get_utf8(&city_result.entry, result->region_code,
                           sizeof(result->region_code), "subdivisions", "0", "iso_code");
            {
                MMDB_entry_data_s data;
                int rc = MMDB_get_value(&city_result.entry, &data,
                                        "subdivisions", "0", "names", "en", NULL);
                if (rc == MMDB_SUCCESS && data.has_data &&
                    data.type == MMDB_DATA_TYPE_UTF8_STRING)
                    copy_text(result->region_name, sizeof(result->region_name),
                              data.utf8_string, data.data_size);
            }
            (void)get_utf8(&city_result.entry, result->city,
                           sizeof(result->city), "city", "names", "en");
            network_string(ip, city_result.netmask,
                           result->network, sizeof(result->network));
        }
    }

    if (context->asn_open) {
        int gai_error = 0;
        int mmdb_error = MMDB_SUCCESS;
        asn_result = MMDB_lookup_string(&context->asn, ip, &gai_error, &mmdb_error);
        if (gai_error != 0 || mmdb_error != MMDB_SUCCESS) {
            error = 1;
        } else if (asn_result.found_entry) {
            asn_found = 1;
            (void)get_uint32(&asn_result.entry, &result->asn,
                             "autonomous_system_number", NULL);
            (void)get_utf8(&asn_result.entry, result->organization,
                           sizeof(result->organization),
                           "autonomous_system_organization", NULL, NULL);
            if (result->network[0] == '\0')
                network_string(ip, asn_result.netmask,
                               result->network, sizeof(result->network));
        }
    }

    if (city_found && asn_found)
        (void)snprintf(result->source, sizeof(result->source),
                       "GeoLite2-City+GeoLite2-ASN");
    else if (city_found)
        (void)snprintf(result->source, sizeof(result->source), "GeoLite2-City");
    else if (asn_found)
        (void)snprintf(result->source, sizeof(result->source), "GeoLite2-ASN");

    if (city_found || asn_found)
        (void)snprintf(result->status, sizeof(result->status), "ok");
    else if (error)
        (void)snprintf(result->status, sizeof(result->status), "error");
    else
        (void)snprintf(result->status, sizeof(result->status), "not_found");
}
