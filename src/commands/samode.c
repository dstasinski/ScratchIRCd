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
        int temporary = 0;
        char mode_params[IRCD_MESSAGE_BUFFER_SIZE];

        if (channel == NULL) {
            client_sendf(client, ERR_NOSUCHCHANNEL, server->config.server_name,
                         client->nick, target_name);
            return COMMAND_KEEP_CLIENT;
        }

        member = channel_find_member(channel, client);
        if (member == NULL) {
            if (channel_add_client(channel, client) != 0) return COMMAND_KEEP_CLIENT;
            temporary = 1;
            member = channel_find_member(channel, client);
        } else {
            saved = member->privileges;
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
        else member->privileges = saved;
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
