#include "cloak.h"
#include "client.h"
#include "modes.h"
#include "runtime_config.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <openssl/opensslv.h>
#include <openssl/evp.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
#include <openssl/core_names.h>
#include <openssl/params.h>
#else
#include <openssl/hmac.h>
#endif
#include <stdio.h>
#include <string.h>

static int hmac32(const ServerConfig *config, const char *label,
                  const void *data, size_t data_len, unsigned int *value) {
    unsigned char digest[EVP_MAX_MD_SIZE];
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_MAC *mac = NULL;
    EVP_MAC_CTX *ctx = NULL;
    OSSL_PARAM params[2];
    size_t digest_len = 0U;
#else
    HMAC_CTX *ctx;
    unsigned int digest_len = 0U;
#endif

    if (config == NULL || label == NULL || data == NULL || value == NULL ||
        config->cloak_key[0] == '\0') return -1;

#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    if (mac == NULL) return -1;
    ctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (ctx == NULL) return -1;
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST,
                                                  (char *)"SHA256", 0U);
    params[1] = OSSL_PARAM_construct_end();
    if (EVP_MAC_init(ctx, (const unsigned char *)config->cloak_key,
                     strlen(config->cloak_key), params) != 1 ||
        EVP_MAC_update(ctx, (const unsigned char *)label, strlen(label)) != 1 ||
        EVP_MAC_update(ctx, (const unsigned char *)"|", 1U) != 1 ||
        EVP_MAC_update(ctx, (const unsigned char *)data, data_len) != 1 ||
        EVP_MAC_final(ctx, digest, &digest_len, sizeof(digest)) != 1 ||
        digest_len < 4U) {
        EVP_MAC_CTX_free(ctx);
        return -1;
    }
    EVP_MAC_CTX_free(ctx);
#else
    ctx = HMAC_CTX_new();
    if (ctx == NULL) return -1;
    if (HMAC_Init_ex(ctx, config->cloak_key, (int)strlen(config->cloak_key),
                     EVP_sha256(), NULL) != 1 ||
        HMAC_Update(ctx, (const unsigned char *)label, strlen(label)) != 1 ||
        HMAC_Update(ctx, (const unsigned char *)"|", 1U) != 1 ||
        HMAC_Update(ctx, (const unsigned char *)data, data_len) != 1 ||
        HMAC_Final(ctx, digest, &digest_len) != 1 || digest_len < 4U) {
        HMAC_CTX_free(ctx);
        return -1;
    }
    HMAC_CTX_free(ctx);
#endif

    *value = ((unsigned int)digest[0] << 24) |
             ((unsigned int)digest[1] << 16) |
             ((unsigned int)digest[2] << 8) |
             (unsigned int)digest[3];
    return 0;
}

static int cloak_ipv4(const ServerConfig *config, const struct in_addr *address,
                      char *out, size_t out_size) {
    unsigned char full[4], p24[3], p16[2];
    unsigned int a, b, c;
    memcpy(full, &address->s_addr, sizeof(full));
    memcpy(p24, full, sizeof(p24));
    memcpy(p16, full, sizeof(p16));
    if (hmac32(config, "ipv4/full", full, sizeof(full), &a) != 0 ||
        hmac32(config, "ipv4/24", p24, sizeof(p24), &b) != 0 ||
        hmac32(config, "ipv4/16", p16, sizeof(p16), &c) != 0) return -1;
    return snprintf(out, out_size, "%08X.%08X.%08X.IP", a, b, c) > 0 ? 0 : -1;
}

static int cloak_ipv6(const ServerConfig *config, const struct in6_addr *address,
                      char *out, size_t out_size) {
    unsigned int a, b, c;
    if (hmac32(config, "ipv6/full", address->s6_addr, 16U, &a) != 0 ||
        hmac32(config, "ipv6/64", address->s6_addr, 8U, &b) != 0 ||
        hmac32(config, "ipv6/48", address->s6_addr, 6U, &c) != 0) return -1;
    return snprintf(out, out_size, "%08X.%08X.%08X.IP", a, b, c) > 0 ? 0 : -1;
}

static int cloak_hostname(const ServerConfig *config, const char *host,
                          char *out, size_t out_size) {
    char normalized[IRC_HOST_MAX + 1U];
    const char *suffix = NULL;
    unsigned int token;
    size_t i, length;
    int written;
    length = strlen(host);
    if (length == 0U || length > IRC_HOST_MAX) return -1;
    for (i = 0U; i < length; ++i)
        normalized[i] = (char)tolower((unsigned char)host[i]);
    normalized[length] = '\0';
    for (i = 0U; i + 1U < length; ++i) {
        if (normalized[i] == '.' && isalpha((unsigned char)normalized[i + 1U])) {
            suffix = normalized + i + 1U;
            break;
        }
    }
    if (hmac32(config, "hostname", normalized, length, &token) != 0) return -1;
    if (suffix != NULL && *suffix != '\0')
        written = snprintf(out, out_size, "%s-%08X.%s",
                           config->cloak_prefix, token, suffix);
    else
        written = snprintf(out, out_size, "%s-%08X", config->cloak_prefix, token);
    return written >= 0 && (size_t)written < out_size ? 0 : -1;
}

int cloak_generate(const ServerConfig *config,
                   const char *real_ip, const char *real_host,
                   char *out, size_t out_size) {
    struct in_addr v4;
    struct in6_addr v6;
    if (config == NULL || real_ip == NULL || out == NULL || out_size == 0U ||
        config->cloak_prefix[0] == '\0' || config->cloak_key[0] == '\0') return -1;
    if (real_host != NULL && real_host[0] != '\0')
        return cloak_hostname(config, real_host, out, out_size);
    if (inet_pton(AF_INET, real_ip, &v4) == 1)
        return cloak_ipv4(config, &v4, out, out_size);
    if (inet_pton(AF_INET6, real_ip, &v6) == 1)
        return cloak_ipv6(config, &v6, out, out_size);
    return -1;
}

void cloak_refresh_display_host(const ServerConfig *config, Client *client) {
    char cloak[IRC_HOST_MAX + 1U];
    if (config == NULL || client == NULL) return;
    if (client_mode_has(client->modes, CLIENT_MODE_VHOST)) return;
    if (client_mode_has(client->modes, CLIENT_MODE_CLOAKED) &&
        cloak_generate(config, client->real_ip, client->real_host,
                       cloak, sizeof(cloak)) == 0) {
        (void)snprintf(client->display_host, sizeof(client->display_host), "%s", cloak);
        return;
    }
    (void)snprintf(client->display_host, sizeof(client->display_host), "%s",
                   client->real_host[0] != '\0' ? client->real_host : client->real_ip);
}
