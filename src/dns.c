/**
 * @file dns.c
 * @brief Non-blocking DNS integration using a dedicated resolver thread.
 *
 * getnameinfo() and getaddrinfo() are allowed to block in this file because
 * they execute only on the resolver worker.  The IRC event-loop thread never
 * calls a name-service operation: it writes a request record to a non-blocking
 * pipe and receives completed records through a second pipe watched by poll().
 *
 * Forward-confirmed reverse DNS (FCrDNS) is performed by resolving the PTR
 * hostname back to addresses and requiring the original IPv4/IPv6 address to
 * appear in that result set before the hostname is trusted as client->host.
 */

#include "dns.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

static int write_full(int fd, const void *buffer, size_t length) {
    const unsigned char *cursor = buffer;

    while (length > 0U) {
        ssize_t written = write(fd, cursor, length);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        cursor += (size_t)written;
        length -= (size_t)written;
    }
    return 0;
}

static int read_full(int fd, void *buffer, size_t length) {
    unsigned char *cursor = buffer;

    while (length > 0U) {
        ssize_t received = read(fd, cursor, length);
        if (received == 0) {
            return 0;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        cursor += (size_t)received;
        length -= (size_t)received;
    }
    return 1;
}

static int request_sockaddr(const DnsRequest *request,
                            struct sockaddr_storage *storage,
                            socklen_t *length) {
    memset(storage, 0, sizeof(*storage));

    if (request->address_family == AF_INET) {
        struct sockaddr_in *address = (struct sockaddr_in *)storage;
        address->sin_family = AF_INET;
        if (inet_pton(AF_INET, request->ip, &address->sin_addr) != 1) {
            return -1;
        }
        *length = (socklen_t)sizeof(*address);
        return 0;
    }

    if (request->address_family == AF_INET6) {
        struct sockaddr_in6 *address = (struct sockaddr_in6 *)storage;
        address->sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, request->ip, &address->sin6_addr) != 1) {
            return -1;
        }
        *length = (socklen_t)sizeof(*address);
        return 0;
    }

    return -1;
}

static int address_matches(const DnsRequest *request,
                           const struct sockaddr *address) {
    if (request->address_family == AF_INET && address->sa_family == AF_INET) {
        struct in_addr original;
        const struct sockaddr_in *candidate =
            (const struct sockaddr_in *)address;
        return inet_pton(AF_INET, request->ip, &original) == 1 &&
               memcmp(&original, &candidate->sin_addr, sizeof(original)) == 0;
    }

    if (request->address_family == AF_INET6 && address->sa_family == AF_INET6) {
        struct in6_addr original;
        const struct sockaddr_in6 *candidate =
            (const struct sockaddr_in6 *)address;
        return inet_pton(AF_INET6, request->ip, &original) == 1 &&
               memcmp(&original, &candidate->sin6_addr, sizeof(original)) == 0;
    }

    return 0;
}

static void resolve_request(const DnsRequest *request, DnsResult *result) {
    struct sockaddr_storage storage;
    socklen_t length = 0U;
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *candidate;

    memset(result, 0, sizeof(*result));
    result->client_id = request->client_id;

    if (request_sockaddr(request, &storage, &length) != 0) {
        return;
    }

    if (getnameinfo((const struct sockaddr *)&storage, length,
                    result->reverse_host, sizeof(result->reverse_host),
                    NULL, 0U, NI_NAMEREQD) != 0) {
        result->reverse_host[0] = '\0';
        return;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = request->address_family;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(result->reverse_host, NULL, &hints, &addresses) != 0) {
        return;
    }

    for (candidate = addresses; candidate != NULL; candidate = candidate->ai_next) {
        if (address_matches(request, candidate->ai_addr)) {
            result->verified = 1;
            (void)snprintf(result->forward_host, sizeof(result->forward_host),
                           "%s", result->reverse_host);
            break;
        }
    }

    freeaddrinfo(addresses);
}

static void *resolver_main(void *arg) {
    DnsResolver *resolver = arg;
    DnsRequest request;

    while (read_full(resolver->request_read_fd, &request, sizeof(request)) == 1) {
        DnsResult result;
        resolve_request(&request, &result);
        if (write_full(resolver->result_write_fd, &result, sizeof(result)) != 0) {
            break;
        }
    }
    return NULL;
}

int dns_resolver_init(DnsResolver *resolver) {
    int requests[2] = {-1, -1};
    int results[2] = {-1, -1};

    if (resolver == NULL) {
        return -1;
    }
    memset(resolver, 0, sizeof(*resolver));
    resolver->request_read_fd = -1;
    resolver->request_write_fd = -1;
    resolver->result_read_fd = -1;
    resolver->result_write_fd = -1;

    if (pipe(requests) != 0 || pipe(results) != 0) {
        if (requests[0] >= 0) close(requests[0]);
        if (requests[1] >= 0) close(requests[1]);
        if (results[0] >= 0) close(results[0]);
        if (results[1] >= 0) close(results[1]);
        return -1;
    }

    resolver->request_read_fd = requests[0];
    resolver->request_write_fd = requests[1];
    resolver->result_read_fd = results[0];
    resolver->result_write_fd = results[1];

    if (set_nonblocking(resolver->request_write_fd) != 0 ||
        set_nonblocking(resolver->result_read_fd) != 0) {
        dns_resolver_destroy(resolver);
        return -1;
    }

    if (pthread_create(&resolver->thread, NULL, resolver_main, resolver) != 0) {
        dns_resolver_destroy(resolver);
        return -1;
    }

    resolver->running = 1;
    return 0;
}

void dns_resolver_destroy(DnsResolver *resolver) {
    if (resolver == NULL) {
        return;
    }

    if (resolver->request_write_fd >= 0) {
        close(resolver->request_write_fd);
        resolver->request_write_fd = -1;
    }

    if (resolver->running) {
        (void)pthread_join(resolver->thread, NULL);
        resolver->running = 0;
    }

    if (resolver->request_read_fd >= 0) close(resolver->request_read_fd);
    if (resolver->result_read_fd >= 0) close(resolver->result_read_fd);
    if (resolver->result_write_fd >= 0) close(resolver->result_write_fd);
    resolver->request_read_fd = -1;
    resolver->result_read_fd = -1;
    resolver->result_write_fd = -1;
}

int dns_resolver_submit(DnsResolver *resolver, uint64_t client_id,
                        int address_family, const char *ip) {
    DnsRequest request;
    ssize_t written;

    if (resolver == NULL || !resolver->running || ip == NULL) {
        return -1;
    }

    memset(&request, 0, sizeof(request));
    request.client_id = client_id;
    request.address_family = address_family;
    if (snprintf(request.ip, sizeof(request.ip), "%s", ip) < 0) {
        return -1;
    }

    do {
        written = write(resolver->request_write_fd, &request, sizeof(request));
    } while (written < 0 && errno == EINTR);

    return written == (ssize_t)sizeof(request) ? 0 : -1;
}

int dns_resolver_result_fd(const DnsResolver *resolver) {
    return resolver != NULL ? resolver->result_read_fd : -1;
}

int dns_resolver_read_result(DnsResolver *resolver, DnsResult *result) {
    ssize_t received;

    if (resolver == NULL || result == NULL || resolver->result_read_fd < 0) {
        return -1;
    }

    do {
        received = read(resolver->result_read_fd, result, sizeof(*result));
    } while (received < 0 && errno == EINTR);

    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    if (received == (ssize_t)sizeof(*result)) {
        return 1;
    }
    return received == 0 ? 0 : -1;
}
