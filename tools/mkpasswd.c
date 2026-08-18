/**
 * @file mkpasswd.c
 * @brief Generate an encoded Argon2id password hash for ScratchIRCd config.
 *
 * Usage: scratchircd-mkpasswd <password>
 *
 * The encoded result can be pasted directly into oper_password_hash. Salt is
 * generated from Linux getrandom(); short reads and EINTR are handled so a
 * partially filled salt can never be used accidentally.
 */

#include "config.h"

#include <argon2.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>

static int fill_random(uint8_t *buffer, size_t length) {
    size_t used = 0U;

    while (used < length) {
        ssize_t got = getrandom(buffer + used, length - used, 0);
        if (got > 0) {
            used += (size_t)got;
            continue;
        }
        if (got < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    uint8_t salt[IRCD_ARGON2_SALT_BYTES];
    char encoded[IRCD_OPER_HASH_MAX + 1U];
    int result;

    if (argc != 2 || argv[1][0] == '\0') {
        fprintf(stderr, "usage: %s <password>\n", argv[0]);
        return 2;
    }

    if (fill_random(salt, sizeof(salt)) != 0) {
        fprintf(stderr, "unable to obtain secure random salt\n");
        return 1;
    }

    result = argon2id_hash_encoded(IRCD_ARGON2_TIME_COST,
                                   IRCD_ARGON2_MEMORY_COST_KIB,
                                   IRCD_ARGON2_PARALLELISM,
                                   argv[1], strlen(argv[1]),
                                   salt, sizeof(salt),
                                   IRCD_ARGON2_HASH_BYTES,
                                   encoded, sizeof(encoded));
    if (result != ARGON2_OK) {
        fprintf(stderr, "argon2id: %s\n", argon2_error_message(result));
        return 1;
    }

    puts(encoded);
    return 0;
}
