/**
 * @file mode_persist.c
 * @brief ChanServ/user-mode-aware wrapper around the existing MODE implementation.
 *
 * CMake compiles mode.c with command_mode renamed to command_mode_core. This
 * wrapper leaves the mature parser untouched while adding ChanServ persistence
 * and the small set of user modes that need behavior beyond simple bit flips.
 */

#include "commands.h"
#include "channel_policy.h"
#include "chanserv.h"
#include "modes.h"
#include "numerics.h"
#include "usermode_policy.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

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

static void send_channel_modes(Server *server, Client *client, Channel *channel) {
    static const char booleans[] = "AciKMmnOprRSstTVz";
    char modes[128] = "+";
    /* Five possible parameters: key (63), limit (20 decimal digits), join
     * throttle (21), and two channel redirects (63 each), plus separators.
     * 256 bytes therefore covers every representable channel state. */
    char params[256] = "";
    char combined[512];
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
        modes[used++] = 'k'; append_param(params, sizeof(params), channel->key);
    }
    if (channel->user_limit != 0U && used + 1U < sizeof(modes)) {
        modes[used++] = 'l';
        (void)snprintf(number, sizeof(number), "%zu", channel->user_limit);
        append_param(params, sizeof(params), number);
    }
    if (channel->join_throttle_count != 0U && used + 1U < sizeof(modes)) {
        modes[used++] = 'j';
        (void)snprintf(number, sizeof(number), "%u:%u",
                       channel->join_throttle_count, channel->join_throttle_seconds);
        append_param(params, sizeof(params), number);
    }
    if (channel->limit_redirect[0] != '\0' && used + 1U < sizeof(modes)) {
        modes[used++] = 'L'; append_param(params, sizeof(params), channel->limit_redirect);
    }
    if (channel->ban_redirect[0] != '\0' && used + 1U < sizeof(modes)) {
        modes[used++] = 'B'; append_param(params, sizeof(params), channel->ban_redirect);
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

/**
 * Validate +B/+L destinations before the mature MODE parser stores them.
 * This prevents silent truncation and pointless self-redirects.
 */
static int validate_redirect_modes(Server *server, Client *client,
                                   Channel *channel, const char *params) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *target, *modes, *argv[IRC_MODE_MAX_PARAMS], *token;
    size_t argc = 0U, argi = 0U, i;
    char sign = '+';

    if (server == NULL || client == NULL || channel == NULL || params == NULL) return 1;
    (void)snprintf(copy, sizeof(copy), "%s", params);
    target = strtok(copy, " ");
    modes = strtok(NULL, " ");
    while (argc < IRC_MODE_MAX_PARAMS && (token = strtok(NULL, " ")) != NULL)
        argv[argc++] = token;
    (void)target;
    if (modes == NULL) return 1;

    for (i = 0U; modes[i] != '\0'; ++i) {
        char letter = modes[i];
        const char *param = argi < argc ? argv[argi] : NULL;
        int consumes = 0;
        if (letter == '+' || letter == '-') { sign = letter; continue; }

        if (letter == 'q' || letter == 'a' || letter == 'o' ||
            letter == 'h' || letter == 'v') consumes = param != NULL;
        else if (letter == 'k') consumes = sign == '+' ? param != NULL : param != NULL;
        else if ((letter == 'l' || letter == 'j' || letter == 'L' || letter == 'B') && sign == '+')
            consumes = param != NULL;
        else if ((letter == 'b' || letter == 'e' || letter == 'I') && param != NULL)
            consumes = 1;

        if ((letter == 'L' || letter == 'B') && sign == '+') {
            if (param == NULL || strchr(IRC_CHANNEL_PREFIXES, param[0]) == NULL ||
                strlen(param) > IRC_CHANNEL_NAME_MAX ||
                strcasecmp(param, channel->name) == 0) {
                client_sendf(client, ERR_NOSUCHCHANNEL,
                             server->config.server_name, client->nick,
                             param != NULL ? param : "*");
                return 0;
            }
        }
        if (consumes) ++argi;
    }
    return 1;
}

/**
 * Handle user modes whose policy cannot be represented by mode.c's simple
 * self-settable table. Returns 1 when at least one special mode was consumed.
 */
static int handle_special_user_modes(Server *server, Client *client,
                                     const char *target, const char *mode_string,
                                     char *filtered, size_t filtered_size) {
    Client *target_client;
    char sign = '+';
    char last_output_sign = '\0';
    size_t used = 0U;
    size_t i;
    int consumed = 0;
    int is_oper;

    if (server == NULL || client == NULL || target == NULL || mode_string == NULL ||
        filtered == NULL || filtered_size == 0U) return 0;
    filtered[0] = '\0';
    target_client = hash_get(&server->clients_by_nick, target);
    if (target_client == NULL || target_client != client) return 0;

    is_oper = client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN);
    for (i = 0U; mode_string[i] != '\0'; ++i) {
        char letter = mode_string[i];
        if (letter == '+' || letter == '-') {
            sign = letter;
            continue;
        }

        if (letter == 'x') {
            consumed = 1;
            if (sign == '+') usermode_apply_cloak(server, client);
            else usermode_remove_cloak(client);
            continue;
        }
        if (letter == 'H' || letter == 'I' || letter == 'W' ||
            letter == 'g' || letter == 's') {
            ClientModeSet bit;
            consumed = 1;
            if (!is_oper) {
                client_sendf(client, ERR_NOPRIVILEGES,
                             server->config.server_name, client->nick);
                continue;
            }
            switch (letter) {
                case 'H': bit = CLIENT_MODE_HIDE_OPER; break;
                case 'I': bit = CLIENT_MODE_HIDE_IDLE; break;
                case 'W': bit = CLIENT_MODE_WHOIS_NOTICE; break;
                case 'g': bit = CLIENT_MODE_GLOBALS; break;
                default:  bit = CLIENT_MODE_SERVER_NOTICES; break;
            }
            if (sign == '+') client->modes = client_mode_add(client->modes, bit);
            else client->modes = client_mode_remove(client->modes, bit);
            continue;
        }

        if (last_output_sign != sign && used + 1U < filtered_size) {
            filtered[used++] = sign;
            last_output_sign = sign;
        }
        if (used + 1U < filtered_size) filtered[used++] = letter;
        filtered[used] = '\0';
    }
    return consumed;
}

CommandResult command_mode(Server *server, Client *client, char *params) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *target;
    char *mode_string;
    char channel_name[IRC_CHANNEL_NAME_MAX + 1U] = "";
    Channel *channel = NULL;
    unsigned int old_join_count = 0U;
    unsigned int old_join_seconds = 0U;
    CommandResult result;

    if (params == NULL) return command_mode_core(server, client, params);
    (void)snprintf(copy, sizeof(copy), "%s", params);
    target = strtok(copy, " ");
    mode_string = strtok(NULL, " ");

    if (target != NULL && strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL) {
        (void)snprintf(channel_name, sizeof(channel_name), "%s", target);
        channel = hash_get(&server->channels_by_name, channel_name);
        if (mode_string == NULL && channel != NULL) {
            if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
            send_channel_modes(server, client, channel);
            return COMMAND_KEEP_CLIENT;
        }
        if (!check_mlock(server, client, channel, mode_string))
            return COMMAND_KEEP_CLIENT;
        if (!validate_redirect_modes(server, client, channel, params))
            return COMMAND_KEEP_CLIENT;
        if (channel != NULL) {
            old_join_count = channel->join_throttle_count;
            old_join_seconds = channel->join_throttle_seconds;
        }
    } else if (target != NULL && mode_string != NULL) {
        char filtered[128];
        if (handle_special_user_modes(server, client, target, mode_string,
                                      filtered, sizeof(filtered))) {
            char forwarded[IRCD_MESSAGE_BUFFER_SIZE];
            if (filtered[0] == '\0')
                (void)snprintf(forwarded, sizeof(forwarded), "%s", target);
            else
                (void)snprintf(forwarded, sizeof(forwarded), "%s %s", target, filtered);
            return command_mode_core(server, client, forwarded);
        }
    }

    result = command_mode_core(server, client, params);

    if (channel_name[0] != '\0') {
        channel = hash_get(&server->channels_by_name, channel_name);
        if (channel != NULL) {
            if (old_join_count != channel->join_throttle_count ||
                old_join_seconds != channel->join_throttle_seconds)
                channel_join_throttle_clear(channel);
            chanserv_persist_channel(server, channel);
        }
    }
    return result;
}
