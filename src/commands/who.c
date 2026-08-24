/**
 * @file who.c
 * @brief Implementation of the IRC WHO command.
 *
 * WHO supports a channel target, a nickname target, or a general query. User
 * mode +i is respected for general and channel WHO. The hostname field is
 * always the client's public display_host; real IP/DNS identity is never
 * exposed here.
 */

#include "commands.h"
#include "modes.h"
#include "numerics.h"
#include "visibility.h"

#include <stdio.h>
#include <string.h>

static void send_who_reply(Server *server, Client *requester,
                           Client *subject, Channel *channel) {
    char status[8];
    size_t used = 1U;
    ChannelMember *member = channel != NULL ? channel_find_member(channel, subject) : NULL;

    status[0] = subject->away[0] != '\0' ? 'G' : 'H';
    status[1] = '\0';

    if (client_mode_has(subject->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN) &&
        (!client_mode_has(subject->modes, CLIENT_MODE_HIDE_OPER) || visibility_is_oper(requester))) {
        status[used++] = '*';
    }
    if (member != NULL && used + 1U < sizeof(status)) {
        char prefix = channel_privilege_prefix(member->privileges);
        if (prefix != '\0') status[used++] = prefix;
    }
    status[used] = '\0';

    client_sendf(requester, RPL_WHOREPLY,
                 server->config.server_name, requester->nick,
                 channel != NULL ? channel->name : "*",
                 subject->user, subject->display_host,
                 server->config.server_name, subject->nick,
                 status, 0, subject->realname);
}

CommandResult command_who(Server *server, Client *client, char *params) {
    char *mask = params != NULL ? strtok(params, " ") : NULL;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;

    if (mask != NULL && strchr(IRC_CHANNEL_PREFIXES, mask[0]) != NULL) {
        Channel *channel = hash_get(&server->channels_by_name, mask);
        if (channel != NULL && visibility_names_channel(client, channel)) {
            ChannelMember *member;
            for (member = channel->members; member != NULL; member = member->next) {
                if (visibility_who_user(client, member->client))
                    send_who_reply(server, client, member->client, channel);
            }
        }
        client_sendf(client, RPL_ENDOFWHO, server->config.server_name, client->nick, mask);
        return COMMAND_KEEP_CLIENT;
    }

    if (mask != NULL && strcmp(mask, "0") != 0 && strcmp(mask, "*") != 0) {
        Client *subject = hash_get(&server->clients_by_nick, mask);
        if (subject != NULL && visibility_who_user(client, subject))
            send_who_reply(server, client, subject, NULL);
        client_sendf(client, RPL_ENDOFWHO, server->config.server_name, client->nick, mask);
        return COMMAND_KEEP_CLIENT;
    }

    for (size_t i = 0U; i < server->client_count; ++i) {
        Client *subject = server->clients[i];
        if (visibility_who_user(client, subject)) send_who_reply(server, client, subject, NULL);
    }

    client_sendf(client, RPL_ENDOFWHO, server->config.server_name, client->nick,
                 mask != NULL ? mask : "*");
    return COMMAND_KEEP_CLIENT;
}
