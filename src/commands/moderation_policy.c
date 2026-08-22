/**
 * @file moderation_policy.c
 * @brief Thin PRIVMSG/NOTICE policy wrapper for oper-controlled +D and +M.
 *
 * The mature message handlers are compiled under internal names. This wrapper
 * enforces moderation state before normal IRC delivery policy runs.
 */

#include "commands.h"
#include "config.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

CommandResult command_privmsg_core(Server *server, Client *client, char *params);
CommandResult command_notice_core(Server *server, Client *client, char *params);

static int is_oper_or_above(const Client *client) {
    return client != NULL &&
           client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN);
}

static char *message_target(char *copy) {
    if (copy == NULL) return NULL;
    return strtok(copy, " ");
}

static int muted_channel_send(Server *server, Client *client,
                              const char *target) {
    if (target == NULL || strchr(IRC_CHANNEL_PREFIXES, target[0]) == NULL)
        return 0;
    if (!client_mode_has(client->modes, CLIENT_MODE_CHANNEL_MUTE)) return 0;
    client_sendf(client, ERR_CANNOTSENDTOCHAN,
                 server->config.server_name, client->nick, target,
                 "channel messaging disabled by user mode +M");
    return 1;
}

static int private_deaf_blocks_privmsg(Server *server, Client *sender,
                                       const char *target_name) {
    Client *destination;
    if (target_name == NULL || strchr(IRC_CHANNEL_PREFIXES, target_name[0]) != NULL)
        return 0;
    destination = hash_get(&server->clients_by_nick, target_name);
    if (destination == NULL) return 0; /* Virtual services and unknown targets use core handling. */

    /* A +D recipient accepts private traffic only from IRCops/netadmins. */
    if (client_mode_has(destination->modes, CLIENT_MODE_PRIVATE_DEAF) &&
        !is_oper_or_above(sender)) {
        client_sendf(sender, ":%s!%s@%s NOTICE %s :I cannot send or receive private messages.",
                     destination->nick, destination->user, destination->display_host,
                     sender->nick);
        return 1;
    }

    /* A +D sender may initiate private traffic only to IRCops/netadmins. */
    if (client_mode_has(sender->modes, CLIENT_MODE_PRIVATE_DEAF) &&
        !is_oper_or_above(destination)) return 1;

    return 0;
}

static int private_deaf_blocks_notice(Server *server, Client *sender,
                                      const char *target_name) {
    Client *destination;
    if (target_name == NULL || strchr(IRC_CHANNEL_PREFIXES, target_name[0]) != NULL)
        return 0;
    destination = hash_get(&server->clients_by_nick, target_name);
    if (destination == NULL) return 0;

    if (client_mode_has(destination->modes, CLIENT_MODE_PRIVATE_DEAF) &&
        !is_oper_or_above(sender)) return 1;
    if (client_mode_has(sender->modes, CLIENT_MODE_PRIVATE_DEAF) &&
        !is_oper_or_above(destination)) return 1;
    return 0;
}

CommandResult command_privmsg(Server *server, Client *client, char *params) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *target;
    if (params == NULL) return command_privmsg_core(server, client, params);
    (void)snprintf(copy, sizeof(copy), "%s", params);
    target = message_target(copy);
    if (muted_channel_send(server, client, target)) return COMMAND_KEEP_CLIENT;
    if (private_deaf_blocks_privmsg(server, client, target)) return COMMAND_KEEP_CLIENT;
    return command_privmsg_core(server, client, params);
}

CommandResult command_notice(Server *server, Client *client, char *params) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
    char *target;
    if (params == NULL) return command_notice_core(server, client, params);
    (void)snprintf(copy, sizeof(copy), "%s", params);
    target = message_target(copy);
    if (muted_channel_send(server, client, target)) return COMMAND_KEEP_CLIENT;
    if (private_deaf_blocks_notice(server, client, target)) return COMMAND_KEEP_CLIENT;
    return command_notice_core(server, client, params);
}
