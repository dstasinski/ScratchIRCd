/**
 * @file mode_control.c
 * @brief Outer MODE wrapper for controlled and identity-affecting user modes.
 *
 * mode_persist.c is compiled as command_mode_policy_core(). This wrapper keeps
 * ordinary MODE and ChanServ behavior intact while preventing clients from
 * changing +D/+M themselves, owning +x display-host transitions, and ensuring
 * MODE <self> reports all flags.
 */

#include "commands.h"
#include "cloak.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_mode_policy_core(Server *server, Client *client, char *params);

static ClientModeSet bit_for_letter(char letter) {
    switch (letter) {
        case 'B': return CLIENT_MODE_BOT;
        case 'D': return CLIENT_MODE_PRIVATE_DEAF;
        case 'M': return CLIENT_MODE_CHANNEL_MUTE;
        case 'd': return CLIENT_MODE_DEAF;
        case 'g': return CLIENT_MODE_GLOBALS;
        case 'H': return CLIENT_MODE_HIDE_OPER;
        case 'h': return CLIENT_MODE_HELPOP;
        case 'I': return CLIENT_MODE_HIDE_IDLE;
        case 'i': return CLIENT_MODE_INVISIBLE;
        case 'N': return CLIENT_MODE_NETADMIN;
        case 'o': return CLIENT_MODE_OPER;
        case 'p': return CLIENT_MODE_PRIVATE;
        case 'R': return CLIENT_MODE_REGONLY_MSG;
        case 'r': return CLIENT_MODE_REGISTERED;
        case 'S': return CLIENT_MODE_SERVICE;
        case 's': return CLIENT_MODE_SERVER_NOTICES;
        case 'T': return CLIENT_MODE_NO_CTCP;
        case 't': return CLIENT_MODE_VHOST;
        case 'V': return CLIENT_MODE_WEBIRC;
        case 'W': return CLIENT_MODE_WHOIS_NOTICE;
        case 'w': return CLIENT_MODE_WALLOPS;
        case 'x': return CLIENT_MODE_CLOAKED;
        case 'z': return CLIENT_MODE_SECURE;
        default: return 0U;
    }
}

static void send_full_user_modes(Server *server, Client *client) {
    static const char letters[] = "BDMdghHiINopRrSsTtVWwxz";
    char modes[64];
    size_t used = 0U;
    size_t i;
    modes[used++] = '+';
    for (i = 0U; letters[i] != '\0' && used + 1U < sizeof(modes); ++i) {
        ClientModeSet bit = bit_for_letter(letters[i]);
        if (bit != 0U && client_mode_has(client->modes, bit)) modes[used++] = letters[i];
    }
    modes[used] = '\0';
    client_sendf(client, RPL_UMODEIS,
                 server->config.server_name, client->nick, modes);
}

CommandResult command_mode(Server *server, Client *client, char *params) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char filtered_modes[128];
    char rebuilt[IRCD_MESSAGE_BUFFER_SIZE];
    char *target;
    char *modes;
    char *rest;
    size_t used = 0U;
    size_t i;
    char sign = '+';
    char last_sign = '\0';
    int handled_x = 0;

    if (params == NULL) return command_mode_policy_core(server, client, params);
    (void)snprintf(copy, sizeof(copy), "%s", params);
    target = strtok(copy, " ");
    modes = strtok(NULL, " ");
    rest = strtok(NULL, "");

    if (target != NULL && modes == NULL && strcmp(target, client->nick) == 0) {
        if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
        send_full_user_modes(server, client);
        return COMMAND_KEEP_CLIENT;
    }

    if (target == NULL || modes == NULL || strcmp(target, client->nick) != 0 ||
        strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL) {
        return command_mode_policy_core(server, client, params);
    }
    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    filtered_modes[0] = '\0';
    for (i = 0U; modes[i] != '\0'; ++i) {
        char letter = modes[i];
        if (letter == '+' || letter == '-') {
            sign = letter;
            continue;
        }
        if (letter == 'D' || letter == 'M') {
            client_sendf(client, ERR_NOPRIVILEGES,
                         server->config.server_name, client->nick);
            continue;
        }
        if (letter == 'x') {
            handled_x = 1;
            if (sign == '+') {
                char generated[IRC_HOST_MAX + 1U];
                if (server->config.cloak_key[0] == '\0' ||
                    cloak_generate(&server->config, client->real_ip, client->real_host,
                                   generated, sizeof(generated)) != 0) {
                    client_sendf(client, ":%s NOTICE %s :MODE +x unavailable: cloak_key is not configured",
                                 server->config.server_name, client->nick);
                    continue;
                }
                client->modes = client_mode_add(client->modes, CLIENT_MODE_CLOAKED);
            } else {
                client->modes = client_mode_remove(client->modes, CLIENT_MODE_CLOAKED);
            }
            cloak_refresh_display_host(&server->config, client);
            continue;
        }
        if (last_sign != sign && used + 1U < sizeof(filtered_modes)) {
            filtered_modes[used++] = sign;
            last_sign = sign;
        }
        if (used + 1U < sizeof(filtered_modes)) filtered_modes[used++] = letter;
        filtered_modes[used] = '\0';
    }

    if (filtered_modes[0] != '\0') {
        if (rest != NULL && *rest != '\0')
            (void)snprintf(rebuilt, sizeof(rebuilt), "%s %s %s", target, filtered_modes, rest);
        else
            (void)snprintf(rebuilt, sizeof(rebuilt), "%s %s", target, filtered_modes);
        (void)command_mode_policy_core(server, client, rebuilt);
    }
    if (handled_x) send_full_user_modes(server, client);
    return COMMAND_KEEP_CLIENT;
}
