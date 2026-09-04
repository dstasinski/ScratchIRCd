/**
 * @file samode.c
 * @brief Server-authority MODE changes for operators with can_override.
 */

#include "commands.h"
#include "chanserv.h"
#include "message_policy.h"
#include "modes.h"
#include "numerics.h"
#include "oper.h"

#include <stdio.h>
#include <string.h>

CommandResult command_mode_core(Server *server, Client *client, char *params);

static ClientModeSet allowed_user_mode(char letter) {
    switch (letter) {
        case 'B': return CLIENT_MODE_BOT;
        case 'd': return CLIENT_MODE_DEAF;
        case 'g': return CLIENT_MODE_GLOBALS;
        case 'H': return CLIENT_MODE_HIDE_OPER;
        case 'h': return CLIENT_MODE_HELPOP;
        case 'I': return CLIENT_MODE_HIDE_IDLE;
        case 'i': return CLIENT_MODE_INVISIBLE;
        case 'p': return CLIENT_MODE_PRIVATE;
        case 'R': return CLIENT_MODE_REGONLY_MSG;
        case 's': return CLIENT_MODE_SERVER_NOTICES;
        case 'T': return CLIENT_MODE_NO_CTCP;
        case 'W': return CLIENT_MODE_WHOIS_NOTICE;
        case 'w': return CLIENT_MODE_WALLOPS;
        default: return 0U;
    }
}

static int user_limit_exceeds_persistable_range(const char *param) {
    static const char maximum[] = "9223372036854775807";
    const char *digits;
    const char *p;
    size_t length;

    if (param == NULL || *param == '\0') return 0;
    digits = param;
    if (*digits == '+') ++digits;
    if (*digits == '-') return 1;
    if (*digits == '\0') return 0;
    for (p = digits; *p != '\0'; ++p)
        if (*p < '0' || *p > '9') return 0;
    while (digits[0] == '0' && digits[1] != '\0') ++digits;
    length = strlen(digits);
    if (length != sizeof(maximum) - 1U)
        return length > sizeof(maximum) - 1U;
    return strcmp(digits, maximum) > 0;
}

/* SAMODE intentionally bypasses MLOCK by calling the mature core MODE parser
 * directly. Preserve that authority while still enforcing the representation
 * invariant shared by all persistent channel limits. */
static int samode_channel_params_valid(Server *server, Client *client,
                                       const char *rest) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *modes;
    char *argv[IRC_MODE_MAX_PARAMS];
    char *token;
    size_t argc = 0U;
    size_t argi = 0U;
    size_t i;
    char sign = '+';

    if (server == NULL || client == NULL || rest == NULL) return 1;
    (void)snprintf(copy, sizeof(copy), "%s", rest);
    modes = strtok(copy, " ");
    while (argc < IRC_MODE_MAX_PARAMS && (token = strtok(NULL, " ")) != NULL)
        argv[argc++] = token;
    if (modes == NULL) return 1;

    for (i = 0U; modes[i] != '\0'; ++i) {
        char letter = modes[i];
        const char *param = argi < argc ? argv[argi] : NULL;
        int consumes = 0;
        if (letter == '+' || letter == '-') {
            sign = letter;
            continue;
        }
        if (letter == 'q' || letter == 'a' || letter == 'o' ||
            letter == 'h' || letter == 'v')
            consumes = param != NULL;
        else if (letter == 'k')
            consumes = param != NULL;
        else if ((letter == 'l' || letter == 'j' || letter == 'L' || letter == 'B') &&
                 sign == '+')
            consumes = param != NULL;
        else if ((letter == 'b' || letter == 'e' || letter == 'I') && param != NULL)
            consumes = 1;

        if (letter == 'l' && sign == '+' && param != NULL &&
            user_limit_exceeds_persistable_range(param)) {
            client_sendf(client, ERR_NEEDMOREPARAMS,
                         server->config.server_name, client->nick, "SAMODE");
            return 0;
        }
        if (consumes) ++argi;
    }
    return 1;
}

static CommandResult samode_user(Server *server, Client *actor,
                                 Client *target, const char *modes) {
    char sign = '+';
    size_t i;

    if (modes == NULL || *modes == '\0') {
        client_sendf(actor, ERR_NEEDMOREPARAMS, server->config.server_name,
                     actor->nick, "SAMODE");
        return COMMAND_KEEP_CLIENT;
    }

    for (i = 0U; modes[i] != '\0'; ++i) {
        ClientModeSet bit;
        if (modes[i] == '+' || modes[i] == '-') {
            sign = modes[i];
            continue;
        }
        bit = allowed_user_mode(modes[i]);
        if (bit == 0U) {
            client_sendf(actor, ERR_UMODEUNKNOWNFLAG,
                         server->config.server_name, actor->nick);
            continue;
        }
        if (sign == '+') target->modes = client_mode_add(target->modes, bit);
        else target->modes = client_mode_remove(target->modes, bit);
    }

    client_sendf(actor, ":%s NOTICE %s :SAMODE completed for %s",
                 server->config.server_name, actor->nick, target->nick);
    snotice_broadcast(server, SNOTICE_MODERATION,
                      "SAMODE by %s: user %s %s [real_ip=%s]",
                      actor->nick, target->nick, modes, target->real_ip);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_samode(Server *server, Client *client, char *params) {
    char *target_name;
    char *rest;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_OVERRIDE)) {
        client_sendf(client, ERR_NOPRIVILEGES, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (target_name = strtok(params, " ")) == NULL ||
        (rest = strtok(NULL, "")) == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "SAMODE");
        return COMMAND_KEEP_CLIENT;
    }

    if (strchr(IRC_CHANNEL_PREFIXES, target_name[0]) != NULL) {
        Channel *channel = hash_get(&server->channels_by_name, target_name);
        ChannelMember *member;
        ChannelPrivilegeSet saved = 0U;
        ChannelPrivilegeSet saved_manual = 0U;
        ChannelPrivilegeSet saved_service = 0U;
        int temporary = 0;
        char mode_params[IRCD_MESSAGE_BUFFER_SIZE];

        if (channel == NULL) {
            client_sendf(client, ERR_NOSUCHCHANNEL, server->config.server_name,
                         client->nick, target_name);
            return COMMAND_KEEP_CLIENT;
        }
        if (!samode_channel_params_valid(server, client, rest))
            return COMMAND_KEEP_CLIENT;

        member = channel_find_member(channel, client);
        if (member == NULL) {
            if (channel_add_client(channel, client) != 0) return COMMAND_KEEP_CLIENT;
            temporary = 1;
            member = channel_find_member(channel, client);
        } else {
            saved = member->privileges;
            saved_manual = member->manual_privileges;
            saved_service = member->service_privileges;
        }
        if (member == NULL) return COMMAND_KEEP_CLIENT;
        member->privileges |= CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR;

        (void)snprintf(mode_params, sizeof(mode_params), "%s %s", target_name, rest);
        (void)command_mode_core(server, client, mode_params);
        chanserv_persist_channel(server, channel);
        snotice_broadcast(server, SNOTICE_MODERATION,
                          "SAMODE by %s: channel %s %s",
                          client->nick, target_name, rest);

        if (temporary) channel_remove_client(channel, client);
        else {
            member->privileges = saved;
            member->manual_privileges = saved_manual;
            member->service_privileges = saved_service;
        }
        return COMMAND_KEEP_CLIENT;
    }

    {
        Client *target = hash_get(&server->clients_by_nick, target_name);
        char *mode_string = strtok(rest, " ");
        if (target == NULL) {
            client_sendf(client, ERR_NOSUCHNICK, server->config.server_name,
                         client->nick, target_name);
            return COMMAND_KEEP_CLIENT;
        }
        return samode_user(server, client, target, mode_string);
    }
}
