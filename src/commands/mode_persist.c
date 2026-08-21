/**
 * @file mode_persist.c
 * @brief ChanServ-aware wrapper around the existing MODE implementation.
 *
 * CMake compiles mode.c with command_mode renamed to command_mode_core. This
 * wrapper leaves the mature MODE parser untouched while adding ChanServ
 * policy and persistence around it. Boolean MLOCK is checked before a
 * registered-channel MODE runs, accepted parameter/list changes are persisted
 * afterward, and channel-mode queries include all parameter values in numeric
 * 324 while still using RPL_CHANNELMODEIS from numerics.h.
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

static void append_param(char *buffer, size_t size, const char *param) {
    size_t used;
    if (buffer == NULL || size == 0U || param == NULL || *param == '\0') return;
    used = strlen(buffer);
    (void)snprintf(buffer + used, size - used, "%s%s",
                   used != 0U ? " " : "", param);
}

/**
 * Send RPL_CHANNELMODEIS with both mode letters and parameter values.
 *
 * numerics.h defines RPL_CHANNELMODEIS with one final string field after the
 * channel name. The historical MODE implementation passed modes and params as
 * separate varargs, so the params were silently ignored by printf formatting.
 * Build the complete final field here so numeric 324 remains sourced from
 * numerics.h and reports +k/+l/+j/+L/+B values correctly.
 */
static void send_channel_modes(Server *server, Client *client, Channel *channel) {
    static const char booleans[] = "AciKMmnOprRSstTVz";
    char modes[128] = "+";
    char params[IRCD_MESSAGE_BUFFER_SIZE] = "";
    char combined[IRCD_MESSAGE_BUFFER_SIZE];
    char number[64];
    size_t used = 1U;
    size_t i;

    for (i = 0U; booleans[i] != '\0' && used + 1U < sizeof(modes); ++i) {
        ChannelModeSet bit = boolean_bit(booleans[i]);
        if (booleans[i] == 'r') bit = CHANNEL_MODE_REGISTERED;
        if (bit != 0U && channel_mode_has(channel->modes, bit))
            modes[used++] = booleans[i];
    }
    if (channel->key[0] != '\0' && used + 1U < sizeof(modes)) {
        modes[used++] = 'k';
        append_param(params, sizeof(params), channel->key);
    }
    if (channel->user_limit != 0U && used + 1U < sizeof(modes)) {
        modes[used++] = 'l';
        (void)snprintf(number, sizeof(number), "%zu", channel->user_limit);
        append_param(params, sizeof(params), number);
    }
    if (channel->join_throttle_count != 0U && used + 1U < sizeof(modes)) {
        modes[used++] = 'j';
        (void)snprintf(number, sizeof(number), "%u:%u",
                       channel->join_throttle_count,
                       channel->join_throttle_seconds);
        append_param(params, sizeof(params), number);
    }
    if (channel->limit_redirect[0] != '\0' && used + 1U < sizeof(modes)) {
        modes[used++] = 'L';
        append_param(params, sizeof(params), channel->limit_redirect);
    }
    if (channel->ban_redirect[0] != '\0' && used + 1U < sizeof(modes)) {
        modes[used++] = 'B';
        append_param(params, sizeof(params), channel->ban_redirect);
    }
    modes[used] = '\0';

    if (params[0] != '\0')
        (void)snprintf(combined, sizeof(combined), "%s %s", modes, params);
    else
        (void)snprintf(combined, sizeof(combined), "%s", modes);

    client_sendf(client, RPL_CHANNELMODEIS,
                 server->config.server_name, client->nick,
                 channel->name, combined);
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

        /*
         * Intercept a plain MODE #channel query so numeric 324 includes its
         * parameter values. Registration and channel existence checks remain
         * equivalent to the core command path.
         */
        if (mode_string == NULL && channel != NULL) {
            if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
            send_channel_modes(server, client, channel);
            return COMMAND_KEEP_CLIENT;
        }

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
