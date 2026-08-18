/**
 * @file test_modes.c
 * @brief Unit tests for the policy-neutral mode representation helpers.
 */

#include "modes.h"

#include <assert.h>
#include <string.h>

int main(void) {
    ClientModeSet user_modes = 0U;
    ChannelModeSet channel_modes = 0U;
    ChannelPrivilegeSet privileges = 0U;
    char letters[8];

    user_modes = client_mode_add(user_modes, CLIENT_MODE_INVISIBLE);
    user_modes = client_mode_add(user_modes, CLIENT_MODE_SECURE);
    assert(client_mode_has(user_modes, CLIENT_MODE_INVISIBLE));
    assert(client_mode_has(user_modes, CLIENT_MODE_SECURE));
    assert(!client_mode_has(user_modes, CLIENT_MODE_OPER));
    user_modes = client_mode_remove(user_modes, CLIENT_MODE_INVISIBLE);
    assert(!client_mode_has(user_modes, CLIENT_MODE_INVISIBLE));

    channel_modes = channel_mode_add(channel_modes, CHANNEL_MODE_MODERATED);
    channel_modes = channel_mode_add(channel_modes, CHANNEL_MODE_NO_EXTERNAL);
    assert(channel_mode_has(channel_modes, CHANNEL_MODE_MODERATED));
    assert(channel_mode_has(channel_modes, CHANNEL_MODE_NO_EXTERNAL));
    channel_modes = channel_mode_remove(channel_modes, CHANNEL_MODE_MODERATED);
    assert(!channel_mode_has(channel_modes, CHANNEL_MODE_MODERATED));

    privileges = CHANNEL_PRIV_VOICE | CHANNEL_PRIV_HALFOP |
                 CHANNEL_PRIV_OPERATOR | CHANNEL_PRIV_OWNER;
    assert(channel_privilege_has(privileges, CHANNEL_PRIV_OWNER));
    assert(channel_privilege_prefix(privileges) == '~');
    assert(channel_privilege_format(privileges, letters, sizeof(letters)) == 4U);
    assert(strcmp(letters, "qohv") == 0);

    privileges &= ~CHANNEL_PRIV_OWNER;
    assert(channel_privilege_prefix(privileges) == '@');
    privileges &= ~CHANNEL_PRIV_OPERATOR;
    assert(channel_privilege_prefix(privileges) == '%');
    privileges &= ~CHANNEL_PRIV_HALFOP;
    assert(channel_privilege_prefix(privileges) == '+');
    privileges = 0U;
    assert(channel_privilege_prefix(privileges) == '\0');

    return 0;
}
