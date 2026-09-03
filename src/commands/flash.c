/** @file flash.c @brief Server-originated operator announcements via numeric 343. */

#include "commands.h"
#include "modes.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

static int flash_text_fits(const Server *server, const char *text) {
    char maximum_nick[IRC_NICK_MAX + 1U];
    int written;

    if (server == NULL || text == NULL) return 0;
    memset(maximum_nick, 'n', IRC_NICK_MAX);
    maximum_nick[IRC_NICK_MAX] = '\0';
    written = snprintf(NULL, 0, RPL_FLASH, server->config.server_name,
                       maximum_nick, text);
    return written >= 0 && (size_t)written <= IRC_LINE_CONTENT_MAX;
}

static void send_flash(Server *server, Client *target, const char *text) {
    if (target == NULL || !target->registered) return;
    client_sendf(target, RPL_FLASH, server->config.server_name,
                 target->nick, text);
}

CommandResult command_flash(Server *server, Client *client, char *params) {
    char *targets;
    char *text;
    char *target_name;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (targets = strtok(params, " ")) == NULL ||
        (text = strtok(NULL, "")) == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "FLASH");
        return COMMAND_KEEP_CLIENT;
    }
    if (*text == ':') ++text;
    if (*targets == '\0' || *text == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "FLASH");
        return COMMAND_KEEP_CLIENT;
    }
    if (!flash_text_fits(server, text)) {
        client_sendf(client,
                     ":%s 417 %s FLASH :Message would exceed the IRC line limit",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    if (strcmp(targets, "*") == 0) {
        size_t index;
        for (index = 0U; index < server->client_count; ++index)
            send_flash(server, server->clients[index], text);
        return COMMAND_KEEP_CLIENT;
    }

    for (target_name = strtok(targets, ","); target_name != NULL;
         target_name = strtok(NULL, ",")) {
        if (strchr(IRC_CHANNEL_PREFIXES, target_name[0]) != NULL) {
            Channel *channel = hash_get(&server->channels_by_name, target_name);
            ChannelMember *member;
            if (channel == NULL) {
                client_sendf(client, ERR_NOSUCHCHANNEL,
                             server->config.server_name, client->nick, target_name);
                continue;
            }
            for (member = channel->members; member != NULL; member = member->next)
                send_flash(server, member->client, text);
        } else {
            Client *target = hash_get(&server->clients_by_nick, target_name);
            if (target == NULL || !target->registered) {
                client_sendf(client, ERR_NOSUCHNICK,
                             server->config.server_name, client->nick, target_name);
                continue;
            }
            send_flash(server, target, text);
        }
    }
    return COMMAND_KEEP_CLIENT;
}
