/**
 * @file mode_persist.c
 * @brief ChanServ-aware wrapper around the existing MODE implementation.
 *
 * CMake compiles mode.c with command_mode renamed to command_mode_core. This
 * wrapper leaves the mature MODE parser untouched while adding two ChanServ
 * policies: boolean MLOCK is checked before a registered-channel MODE runs,
 * and accepted parameter/list changes are persisted afterward.
 */

#include "commands.h"
#include "chanserv.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_mode_core(Server *server, Client *client, char *params);

static ChannelModeSet boolean_bit(char letter) {
    switch (letter) {
        case 'A': return CHANNEL_MODE_ADMIN_ONLY;
        case 'c': return CHANNEL_MODE_NO_COLOR;
        case 'i': return CHANNEL_MODE_INVITE_ONLY;
        case 'K': return CHANNEL_MODE_NO_KNOCK;
        case 'M': return CHANNEL_MODE_REGONLY_SPEAK;
        case 'm': return CHANNEL_MODE_MODERATED;
        case 'n': return CHANNEL_MODE_NO_EXTERNAL;
        case 'O': return CHANNEL_MODE_OPER_ONLY;
        case 'p': return CHANNEL_MODE_PRIVATE;
        case 'R': return CHANNEL_MODE_REGONLY_JOIN;
        case 'S': return CHANNEL_MODE_STRIP_COLOR;
        case 's': return CHANNEL_MODE_SECRET;
        case 't': return CHANNEL_MODE_TOPIC_LOCK;
        case 'T': return CHANNEL_MODE_NO_NOTICE;
        case 'V': return CHANNEL_MODE_NO_INVITE;
        case 'z': return CHANNEL_MODE_SECURE_ONLY;
        default: return 0U;
    }
}

static int check_mlock(Server *server, Client *client, Channel *channel,
                       const char *mode_string) {
    char sign = '+';
    size_t i;
    if (channel == NULL || mode_string == NULL) return 1;
    for (i = 0U; mode_string[i] != '\0'; ++i) {
        ChannelModeSet bit;
        char letter = mode_string[i];
        if (letter == '+' || letter == '-') { sign = letter; continue; }
        bit = boolean_bit(letter);
        if (bit == 0U) continue;
        if (!chanserv_mode_change_allowed(server, channel, bit, sign == '+')) {
            client_sendf(client, ERR_CANNOTCHANGECHANMODE,
                         server->config.server_name, client->nick, letter,
                         "Mode is locked by ChanServ");
            return 0;
        }
    }
    return 1;
}

CommandResult command_mode(Server *server, Client *client, char *params) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *target;
    char *mode_string;
    char channel_name[IRC_CHANNEL_NAME_MAX + 1U] = "";
    Channel *channel = NULL;
    CommandResult result;

    if (params == NULL) return command_mode_core(server, client, params);
    (void)snprintf(copy, sizeof(copy), "%s", params);
    target = strtok(copy, " ");
    mode_string = strtok(NULL, " ");

    if (target != NULL && strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL) {
        (void)snprintf(channel_name, sizeof(channel_name), "%s", target);
        channel = hash_get(&server->channels_by_name, channel_name);
        if (!check_mlock(server, client, channel, mode_string))
            return COMMAND_KEEP_CLIENT;
    }

    result = command_mode_core(server, client, params);

    if (channel_name[0] != '\0') {
        channel = hash_get(&server->channels_by_name, channel_name);
        if (channel != NULL) chanserv_persist_channel(server, channel);
    }
    return result;
}
