/**
 * @file dnsbl.c
 * @brief Dedicated worker-thread DNSBL resolver.
 */

#include "dnsbl.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* ScratchIRCd targets Linux. Keep fixed-size request/result writes atomic so a
 * saturated nonblocking pipe can fail a whole submission without corrupting
 * the record stream with a partial request. */
_Static_assert(sizeof(DnsblRequest) <= PIPE_BUF, "DnsblRequest must fit in one atomic pipe write");
_Static_assert(sizeof(DnsblResult) <= PIPE_BUF, "DnsblResult must fit in one atomic pipe write");

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
}

static int read_full(int fd, void *buffer, size_t length) {
    unsigned char *cursor = buffer;
    while (length > 0U) {
        ssize_t n = read(fd, cursor, length);
        if (n == 0) return 0;
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        cursor += (size_t)n;
        length -= (size_t)n;
    }
    return 1;
}

static int write_full(int fd, const void *buffer, size_t length) {
    const unsigned char *cursor = buffer;
    while (length > 0U) {
        ssize_t n = write(fd, cursor, length);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        cursor += (size_t)n;
        length -= (size_t)n;
    }
    return 0;
}

/** Build the DNSBL reversed-address label for IPv4 or IPv6. */
static int reverse_ip(const char *ip, char *buffer, size_t size) {
    struct in_addr v4;
    struct in6_addr v6;

    if (inet_pton(AF_INET, ip, &v4) == 1) {
        const unsigned char *b = (const unsigned char *)&v4.s_addr;
        int written = snprintf(buffer, size, "%u.%u.%u.%u",
                               b[3], b[2], b[1], b[0]);
        return written >= 0 && (size_t)written < size ? 0 : -1;
    }

    if (inet_pton(AF_INET6, ip, &v6) == 1) {
        static const char hex[] = "0123456789abcdef";
        size_t used = 0U;
        int i;
        for (i = 15; i >= 0; --i) {
            unsigned char byte = v6.s6_addr[i];
            char lo = hex[byte & 0x0fU];
            char hi = hex[(byte >> 4U) & 0x0fU];
            if (used + 4U >= size) return -1;
            buffer[used++] = lo; buffer[used++] = '.';
            buffer[used++] = hi; buffer[used++] = '.';
        }
        if (used == 0U) return -1;
        buffer[used - 1U] = '\0';
        return 0;
    }
    return -1;
}

static int positive_answer(const struct addrinfo *answers) {
    const struct addrinfo *candidate;
    for (candidate = answers; candidate != NULL; candidate = candidate->ai_next) {
        if (candidate->ai_family == AF_INET && candidate->ai_addr != NULL) {
            const struct sockaddr_in *address = (const struct sockaddr_in *)candidate->ai_addr;
            uint32_t host = ntohl(address->sin_addr.s_addr);
            unsigned int first = (host >> 24U) & 0xffU;
            unsigned int second = (host >> 16U) & 0xffU;
            unsigned int third = (host >> 8U) & 0xffU;
            if (first == 127U && !(second == 255U && third == 255U)) return 1;
        }
    }
    return 0;
}

static int query_zone(const char *reverse, const char *zone) {
    struct addrinfo hints;
    struct addrinfo *answers = NULL;
    char query[IRCD_DNSBL_ZONE_MAX + IRC_IP_MAX * 4U + 80U];
    int rc;
    int listed = 0;

    if (snprintf(query, sizeof(query), "%s.%s", reverse, zone) >= (int)sizeof(query)) return 0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    rc = getaddrinfo(query, NULL, &hints, &answers);
    if (rc == 0) {
        listed = positive_answer(answers);
        freeaddrinfo(answers);
    }
    return listed;
}

static void resolve_request(const DnsblRequest *request, DnsblResult *result) {
    char reversed[256];
    size_t i;
    memset(result, 0, sizeof(*result));
    result->client_id = request->client_id;
    result->completed = 1;
    if (reverse_ip(request->ip, reversed, sizeof(reversed)) != 0) return;
    for (i = 0U; i < request->zone_count; ++i) {
        if (query_zone(reversed, request->zones[i].zone)) {
            result->listed = 1;
            (void)snprintf(result->name, sizeof(result->name), "%s", request->zones[i].name);
            (void)snprintf(result->zone, sizeof(result->zone), "%s", request->zones[i].zone);
            return;
        }
    }
}

static void *resolver_main(void *arg) {
    DnsblResolver *resolver = arg;
    DnsblRequest request;
    while (read_full(resolver->request_read_fd, &request, sizeof(request)) == 1) {
        DnsblResult result;
        resolve_request(&request, &result);
        /* Dropping a congested result is safe: the client deadline fails open. */
        (void)write_full(resolver->result_write_fd, &result, sizeof(result));
    }
    return NULL;
}

int dnsbl_resolver_init(DnsblResolver *resolver) {
    int requests[2] = {-1, -1};
    int results[2] = {-1, -1};
    if (resolver == NULL) return -1;
    memset(resolver, 0, sizeof(*resolver));
    resolver->request_read_fd = resolver->request_write_fd = -1;
    resolver->result_read_fd = resolver->result_write_fd = -1;
    if (pipe(requests) != 0 || pipe(results) != 0) goto fail;
    resolver->request_read_fd = requests[0]; resolver->request_write_fd = requests[1];
    resolver->result_read_fd = results[0]; resolver->result_write_fd = results[1];
    if (set_nonblocking(resolver->request_write_fd) != 0 ||
        set_nonblocking(resolver->result_read_fd) != 0 ||
        set_nonblocking(resolver->result_write_fd) != 0) goto fail;
    if (pthread_create(&resolver->thread, NULL, resolver_main, resolver) != 0) goto fail;
    resolver->running = 1;
    return 0;
fail:
    if (requests[0] >= 0 && resolver->request_read_fd < 0) close(requests[0]);
    if (requests[1] >= 0 && resolver->request_write_fd < 0) close(requests[1]);
    if (results[0] >= 0 && resolver->result_read_fd < 0) close(results[0]);
    if (results[1] >= 0 && resolver->result_write_fd < 0) close(results[1]);
    dnsbl_resolver_destroy(resolver);
    return -1;
}

void dnsbl_resolver_destroy(DnsblResolver *resolver) {
    if (resolver == NULL) return;
    if (resolver->request_write_fd >= 0) { close(resolver->request_write_fd); resolver->request_write_fd = -1; }
    if (resolver->running) { (void)pthread_join(resolver->thread, NULL); resolver->running = 0; }
    if (resolver->request_read_fd >= 0) close(resolver->request_read_fd);
    if (resolver->result_read_fd >= 0) close(resolver->result_read_fd);
    if (resolver->result_write_fd >= 0) close(resolver->result_write_fd);
    resolver->request_read_fd = resolver->result_read_fd = resolver->result_write_fd = -1;
}

int dnsbl_resolver_submit(DnsblResolver *resolver, uint64_t client_id,
                          const char *ip, const DnsblZone *zones,
                          size_t zone_count) {
    DnsblRequest request;
    ssize_t n;
    if (resolver == NULL || !resolver->running || ip == NULL || zone_count > IRCD_MAX_DNSBLS) return -1;
    memset(&request, 0, sizeof(request));
    request.client_id = client_id;
    request.zone_count = zone_count;
    (void)snprintf(request.ip, sizeof(request.ip), "%s", ip);
    if (zone_count > 0U) memcpy(request.zones, zones, zone_count * sizeof(*zones));
    do { n = write(resolver->request_write_fd, &request, sizeof(request)); }
    while (n < 0 && errno == EINTR);
    return n == (ssize_t)sizeof(request) ? 0 : -1;
}

int dnsbl_resolver_result_fd(const DnsblResolver *resolver) {
    return resolver != NULL ? resolver->result_read_fd : -1;
}

int dnsbl_resolver_read_result(DnsblResolver *resolver, DnsblResult *result) {
    ssize_t n;
    if (resolver == NULL || result == NULL || resolver->result_read_fd < 0) return -1;
    do { n = read(resolver->result_read_fd, result, sizeof(*result)); }
    while (n < 0 && errno == EINTR);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
    if (n == (ssize_t)sizeof(*result)) return 1;
    return n == 0 ? 0 : -1;
}
