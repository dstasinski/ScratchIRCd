/**
 * @file whois.c
 * @brief Implementation of the IRC WHOIS command.
 *
 * WHOIS exposes only effective IRC identity to ordinary users.  Channel lists
 * respect user +p, channel +p/+s, and '&' visibility.  IRC operators may see
 * the retained physical socket address, which preserves the future WebIRC
 * distinction between an end-user identity and the authorized gateway peer.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"
#include "visibility.h"

#include <stdio.h>
#include <string.h>

static void append_channel(char *buffer, size_t size, const char *name,
                           char prefix) {
    size_t used = strlen(buffer);
    if (used >= size) {
        return;
    }
    (void)snprintf(buffer + used, size - used, "%s%s%s",
                   used != 0U ? " " : "",
                   prefix != '\0' ? (char[2]){prefix, '\0'} : "", name);
}

CommandResult command_whois(Server *server, Client *client, char *params) {
    char *target_name;
    Client *target;
    char channels[IRC_NAMES_BUFFER_SIZE] = "";
    ClientChannelLink *link;

    if (command_require_registered(client)) {
        return COMMAND_KEEP_CLIENT;
    }

    if (params == NULL || (target_name = strtok(params, " ,")) == NULL) {
        client_sendf(client, ERR_NONICKNAMEGIVEN,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    target = hash_get(&server->clients_by_nick, target_name);
    if (target == NULL) {
        client_sendf(client, ERR_NOSUCHNICK,
                     server->config.server_name, client->nick, target_name);
        client_sendf(client, RPL_ENDOFWHOIS,
                     server->config.server_name, client->nick, target_name);
        return COMMAND_KEEP_CLIENT;
    }

    client_sendf(client, RPL_WHOISUSER,
                 server->config.server_name, client->nick,
                 target->nick, target->user, target->host, target->realname);
    client_sendf(client, RPL_WHOISSERVER,
                 server->config.server_name, client->nick,
                 target->nick, server->config.server_name,
                 server->config.network_name);

    if (client_mode_has(target->modes, CLIENT_MODE_REGISTERED)) {
        client_sendf(client, RPL_WHOISREGNICK,
                     server->config.server_name, client->nick, target->nick);
    }
    if (client_mode_has(target->modes, CLIENT_MODE_BOT)) {
        client_sendf(client, RPL_WHOISBOT,
                     server->config.server_name, client->nick, target->nick);
    }
    if (client_mode_has(target->modes, CLIENT_MODE_HELPOP)) {
        client_sendf(client, RPL_WHOISHELPOP,
                     server->config.server_name, client->nick, target->nick);
    }

    if (client_mode_has(target->modes, CLIENT_MODE_NETADMIN) &&
        (!client_mode_has(target->modes, CLIENT_MODE_HIDE_OPER) ||
         visibility_is_oper(client))) {
        client_sendf(client, RPL_WHOISADMIN,
                     server->config.server_name, client->nick, target->nick);
    } else if (client_mode_has(target->modes, CLIENT_MODE_OPER) &&
               (!client_mode_has(target->modes, CLIENT_MODE_HIDE_OPER) ||
                visibility_is_oper(client))) {
        client_sendf(client, RPL_WHOISOPERATOR,
                     server->config.server_name, client->nick, target->nick);
    }

    if (client_mode_has(target->modes, CLIENT_MODE_SECURE)) {
        client_sendf(client, RPL_WHOISSECURE,
                     server->config.server_name, client->nick, target->nick,
                     "is using a secure connection");
    }

    for (link = target->channels; link != NULL; link = link->next) {
        Channel *channel = link->channel;
        ChannelMember *membership;
        if (!visibility_whois_channel(client, target, channel)) {
            continue;
        }
        membership = channel_find_member(channel, target);
        append_channel(channels, sizeof(channels), channel->name,
                       membership != NULL ?
                       channel_privilege_prefix(membership->privileges) : '\0');
    }

    if (channels[0] != '\0') {
        client_sendf(client, RPL_WHOISCHANNELS,
                     server->config.server_name, client->nick,
                     target->nick, channels);
    }

    if (visibility_is_oper(client)) {
        client_sendf(client, RPL_WHOISHOST,
                     server->config.server_name, client->nick,
                     target->nick, target->socket_host, target->socket_ip);
    }

    client_sendf(client, RPL_ENDOFWHOIS,
                 server->config.server_name, client->nick, target->nick);
    return COMMAND_KEEP_CLIENT;
}
