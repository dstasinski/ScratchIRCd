/**
 * @file deafmute.c
 * @brief Operator-controlled DEAF (+D) and MUTE (+M) user modes.
 *
 * DEAF and MUTE are moderation controls. Ordinary clients cannot set or clear
 * them through MODE; only an IRC operator or network administrator may use
 * DEAF/MUTE to change another client's state.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

static int actor_is_oper(const Client *client) {
    return client != NULL &&
           client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN);
}

static CommandResult set_control_mode(Server *server, Client *client, char *params,
                                      const char *command, char mode,
                                      ClientModeSet bit) {
    char *spec;
    Client *target;
    int adding;
    char line[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!actor_is_oper(client)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (spec = strtok(params, " \t")) == NULL ||
        (spec[0] != '+' && spec[0] != '-') || spec[1] == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, command);
        return COMMAND_KEEP_CLIENT;
    }
    if (strtok(NULL, " \t") != NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, command);
        return COMMAND_KEEP_CLIENT;
    }

    adding = spec[0] == '+';
    target = hash_get(&server->clients_by_nick, spec + 1);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK,
                     server->config.server_name, client->nick, spec + 1);
        return COMMAND_KEEP_CLIENT;
    }

    if (adding) target->modes = client_mode_add(target->modes, bit);
    else target->modes = client_mode_remove(target->modes, bit);

    (void)snprintf(line, sizeof(line), ":%s!%s@%s MODE %s %c%c",
                   client->nick, client->user, client->display_host,
                   target->nick, adding ? '+' : '-', mode);
    (void)client_send_line(target, line);
    if (target != client) (void)client_send_line(client, line);

    return COMMAND_KEEP_CLIENT;
}

CommandResult command_deaf(Server *server, Client *client, char *params) {
    return set_control_mode(server, client, params, "DEAF", 'D',
                            CLIENT_MODE_PRIVATE_DEAF);
}

CommandResult command_mute(Server *server, Client *client, char *params) {
    return set_control_mode(server, client, params, "MUTE", 'M',
                            CLIENT_MODE_CHANNEL_MUTE);
}
