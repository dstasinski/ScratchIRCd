/** @file tagmsg.c @brief IRCv3 client-only tag delivery without message text. */

#include "commands.h"
#include "channel_policy.h"
#include "ircv3.h"
#include "modes.h"
#include "nospoof.h"
#include "numerics.h"
#include "presence.h"

#include <string.h>
#include <time.h>

static int is_oper_or_above(const Client *client) {
    return client != NULL &&
           client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN);
}

CommandResult command_tagmsg(Server *server, Client *client, char *params) {
    char *target;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if ((client->capabilities & CLIENT_CAP_MESSAGE_TAGS) == 0U) {
        client_sendf(client, ERR_UNKNOWNCOMMAND, server->config.server_name,
                     client->nick, "TAGMSG");
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL || (target = strtok(params, " ")) == NULL ||
        *target == '\0') {
        client_sendf(client, ERR_NORECIPIENT, server->config.server_name,
                     client->nick, "TAGMSG");
        return COMMAND_KEEP_CLIENT;
    }
    if (strtok(NULL, " ") != NULL || strchr(target, ',') != NULL) {
        client_sendf(client, ERR_TOOMANYTARGETS, server->config.server_name,
                     client->nick, target);
        return COMMAND_KEEP_CLIENT;
    }
    if (!nospoof_version_target_allowed(server, client, target)) {
        client_sendf(client,
                     ":%s NOTICE %s :You must respond to the CTCP VERSION request before joining channels or messaging anyone except an IRC operator or network administrator.",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    /* Unknown/untrusted server tags were stripped by the parser. A TAGMSG
     * with no remaining client-only tags has no useful payload to relay. */
    if (client->ircv3_client_tags[0] == '\0') return COMMAND_KEEP_CLIENT;

    if (strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL) {
        Channel *channel = hash_get(&server->channels_by_name, target);
        ChannelMember *member;
        int banned;
        if (channel == NULL) {
            client_sendf(client, ERR_NOSUCHCHANNEL, server->config.server_name,
                         client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }
        member = channel_find_member(channel, client);
        if (member != NULL &&
            channel_privilege_has(member->privileges, CHANNEL_PRIV_OWNER))
            banned = 0;
        else if (member != NULL &&
                 channel_privilege_has(member->privileges, CHANNEL_PRIV_PROTECTED))
            banned = channel_client_is_banned_protected(channel, client);
        else
            banned = channel_client_is_banned(channel, client);
        if (banned ||
            (member == NULL && channel_mode_has(channel->modes, CHANNEL_MODE_NO_EXTERNAL)) ||
            (channel_mode_has(channel->modes, CHANNEL_MODE_REGONLY_SPEAK) &&
             !client_mode_has(client->modes, CLIENT_MODE_REGISTERED)) ||
            (channel_mode_has(channel->modes, CHANNEL_MODE_MODERATED) &&
             (member == NULL ||
              !channel_privilege_has(member->privileges,
                  CHANNEL_PRIV_VOICE | CHANNEL_PRIV_HALFOP |
                  CHANNEL_PRIV_OPERATOR | CHANNEL_PRIV_PROTECTED |
                  CHANNEL_PRIV_OWNER)))) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN,
                         server->config.server_name, client->nick,
                         channel->name, "channel policy blocks TAGMSG");
            return COMMAND_KEEP_CLIENT;
        }
        if (client_mode_has(client->modes, CLIENT_MODE_CHANNEL_MUTE) &&
            !is_oper_or_above(client) &&
            (member == NULL || member->privileges == 0U)) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN,
                         server->config.server_name, client->nick,
                         channel->name, "channel messaging disabled by user mode +M");
            return COMMAND_KEEP_CLIENT;
        }
        ircv3_broadcast_tagmsg(channel, client, client, channel->name);
    } else {
        Client *destination = hash_get(&server->clients_by_nick, target);
        if (destination == NULL) {
            client_sendf(client, ERR_NOSUCHNICK, server->config.server_name,
                         client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }
        if (presence_silence_matches(destination, client)) return COMMAND_KEEP_CLIENT;
        if (client_mode_has(destination->modes, CLIENT_MODE_REGONLY_MSG) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED)) {
            client_sendf(client, ERR_NONONREG, server->config.server_name,
                         client->nick, destination->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if ((client_mode_has(destination->modes, CLIENT_MODE_PRIVATE_DEAF) &&
             !is_oper_or_above(client)) ||
            (client_mode_has(client->modes, CLIENT_MODE_PRIVATE_DEAF) &&
             !is_oper_or_above(destination)))
            return COMMAND_KEEP_CLIENT;
        ircv3_send_tagmsg(destination, client, destination->nick);
    }
    client->last_activity = time(NULL);
    return COMMAND_KEEP_CLIENT;
}
