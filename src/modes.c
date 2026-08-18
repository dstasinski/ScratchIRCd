/**
 * @file modes.c
 * @brief Small, policy-neutral helpers for ScratchIRCd mode bitsets.
 *
 * No IRC command parsing occurs here.  These helpers exist so command code,
 * services, WHO/WHOIS, and channel display logic manipulate mode state through
 * one documented representation rather than duplicating bit operations.
 */

#include "modes.h"

int client_mode_has(ClientModeSet modes, ClientModeSet mask) {
    return (modes & mask) != 0U;
}

ClientModeSet client_mode_add(ClientModeSet modes, ClientModeSet mask) {
    return modes | mask;
}

ClientModeSet client_mode_remove(ClientModeSet modes, ClientModeSet mask) {
    return modes & ~mask;
}

int channel_mode_has(ChannelModeSet modes, ChannelModeSet mask) {
    return (modes & mask) != 0U;
}

ChannelModeSet channel_mode_add(ChannelModeSet modes, ChannelModeSet mask) {
    return modes | mask;
}

ChannelModeSet channel_mode_remove(ChannelModeSet modes, ChannelModeSet mask) {
    return modes & ~mask;
}

int channel_privilege_has(ChannelPrivilegeSet privileges,
                          ChannelPrivilegeSet mask) {
    return (privileges & mask) != 0U;
}

char channel_privilege_prefix(ChannelPrivilegeSet privileges) {
    if ((privileges & CHANNEL_PRIV_OWNER) != 0U) {
        return '~';
    }
    if ((privileges & CHANNEL_PRIV_OPERATOR) != 0U) {
        return '@';
    }
    if ((privileges & CHANNEL_PRIV_HALFOP) != 0U) {
        return '%';
    }
    if ((privileges & CHANNEL_PRIV_VOICE) != 0U) {
        return '+';
    }
    return '\0';
}

size_t channel_privilege_format(ChannelPrivilegeSet privileges,
                                char *buffer, size_t buffer_size) {
    size_t used = 0U;

    if (buffer == NULL || buffer_size == 0U) {
        return 0U;
    }

#define APPEND_PRIV(bit, letter) \
    do { \
        if ((privileges & (bit)) != 0U && used + 1U < buffer_size) { \
            buffer[used++] = (letter); \
        } \
    } while (0)

    APPEND_PRIV(CHANNEL_PRIV_OWNER, 'q');
    APPEND_PRIV(CHANNEL_PRIV_OPERATOR, 'o');
    APPEND_PRIV(CHANNEL_PRIV_HALFOP, 'h');
    APPEND_PRIV(CHANNEL_PRIV_VOICE, 'v');

#undef APPEND_PRIV

    buffer[used] = '\0';
    return used;
}
