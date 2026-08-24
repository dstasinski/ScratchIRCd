/**
 * @file privmsg.c
 * @brief Implementation of IRC PRIVMSG.
 *
 * Channel delivery enforces membership/speaking policy plus +c/+S color
 * policy. Direct delivery enforces SILENCE/+R/+T. Public prefixes always use
 * display_host, and accepted channel text is persisted after filtering.
 */

#include "commands.h"
#include "chanserv.h"
#include "config.h"
#include "history_db.h"
#include "ircv3.h"
#include "memoserv.h"
#include "message_policy.h"
#include "modes.h"
#include "nickserv.h"
#include "nospoof.h"
#include "numerics.h"
#include "presence.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void store_channel_history(Server *server, Client *client,
                                  const char *target, const char *command,
                                  const char *text) {
    HistoryDb db = {0};
    HistoryRecord record;
    struct timespec now;

    if (server == NULL || client == NULL || target == NULL || text == NULL) return;
    memset(&record, 0, sizeof(record));
    (void)snprintf(record.target, sizeof(record.target), "%s", target);
    (void)snprintf(record.command, sizeof(record.command), "%s", command);
    (void)snprintf(record.nick, sizeof(record.nick), "%s", client->nick);
    (void)snprintf(record.user, sizeof(record.user), "%s", client->user);
    (void)snprintf(record.host, sizeof(record.host), "%s", client->display_host);
    (void)snprintf(record.account, sizeof(record.account), "%s", client->account_name);
    (void)snprintf(record.text, sizeof(record.text), "%s", text);
    if (clock_gettime(CLOCK_REALTIME, &now) == 0)
        record.created_at_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    else
        record.created_at_ms = (int64_t)time(NULL) * 1000;

    if (history_db_open(&db, server->config.history_db) == 0) {
        (void)history_db_add(&db, &record);
        history_db_close(&db);
    }
}

CommandResult command_privmsg(Server *server, Client *client, char *params) {
    char *target;
    char *text;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NORECIPIENT, server->config.server_name,
                     client->nick, "PRIVMSG");
        return COMMAND_KEEP_CLIENT;
    }

    target = strtok(params, " ");
    text = strtok(NULL, "");
    if (target == NULL || *target == '\0') {
        client_sendf(client, ERR_NORECIPIENT, server->config.server_name,
                     client->nick, "PRIVMSG");
        return COMMAND_KEEP_CLIENT;
    }
    if (text == NULL || *text == '\0' || (text[0] == ':' && text[1] == '\0')) {
        client_sendf(client, ERR_NOTEXTTOSEND, server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (*text == ':') ++text;

    if (strcasecmp(target, "NickServ") == 0) {
        int was_identified = client->account_name[0] != '\0';
        nickserv_handle_message(server, client, text);
        if (!was_identified && client->account_name[0] != '\0') {
            ircv3_account_notify(client);
            memoserv_notify_unread(server, client);
        }
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(target, "ChanServ") == 0) {
        chanserv_handle_message(server, client, text);
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(target, "MemoServ") == 0) {
        memoserv_handle_message(server, client, text);
        return COMMAND_KEEP_CLIENT;
    }

    if (strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL) {
        Channel *channel = hash_get(&server->channels_by_name, target);
        ChannelMember *member;
        char stripped[IRCD_MESSAGE_BUFFER_SIZE];
        const char *delivered_text = text;

        if (channel == NULL) {
            client_sendf(client, ERR_NOSUCHCHANNEL, server->config.server_name,
                         client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }
        member = channel_find_member(channel, client);
        if (channel_mode_has(channel->modes, CHANNEL_MODE_NO_EXTERNAL) && member == NULL) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN, server->config.server_name,
                         client->nick, channel->name, "no external messages (+n)");
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_REGONLY_SPEAK) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED)) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN, server->config.server_name,
                         client->nick, channel->name, "registered nickname required (+M)");
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_MODERATED) &&
            (member == NULL || !channel_privilege_has(member->privileges,
             CHANNEL_PRIV_VOICE | CHANNEL_PRIV_HALFOP | CHANNEL_PRIV_OPERATOR |
             CHANNEL_PRIV_PROTECTED | CHANNEL_PRIV_OWNER))) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN, server->config.server_name,
                         client->nick, channel->name, "moderated channel (+m)");
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_NO_COLOR) &&
            message_contains_color(text)) {
            client_sendf(client, ERR_CANNOTSENDTOCHAN, server->config.server_name,
                         client->nick, channel->name, "colors are not permitted (+c)");
            return COMMAND_KEEP_CLIENT;
        }
        if (channel_mode_has(channel->modes, CHANNEL_MODE_STRIP_COLOR)) {
            message_strip_color(text, stripped, sizeof(stripped));
            delivered_text = stripped;
        }

        store_channel_history(server, client, channel->name, "PRIVMSG", delivered_text);
        ircv3_broadcast_message(channel, client, client, "PRIVMSG",
                                channel->name, delivered_text);
    } else {
        Client *destination = hash_get(&server->clients_by_nick, target);
        if (destination == NULL) {
            client_sendf(client, ERR_NOSUCHNICK, server->config.server_name,
                         client->nick, target);
            return COMMAND_KEEP_CLIENT;
        }
        if (nospoof_version_restricted(server, client) &&
            !client_mode_has(destination->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
            client_sendf(client, ":%s NOTICE %s :You must respond to the CTCP VERSION request before messaging ordinary users.",
                         server->config.server_name, client->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (presence_silence_matches(destination, client)) return COMMAND_KEEP_CLIENT;
        if (client_mode_has(destination->modes, CLIENT_MODE_REGONLY_MSG) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED)) {
            client_sendf(client, ERR_NONONREG, server->config.server_name,
                         client->nick, destination->nick);
            return COMMAND_KEEP_CLIENT;
        }
        if (client_mode_has(destination->modes, CLIENT_MODE_NO_CTCP) && text[0] == '\001') {
            client_sendf(client, ERR_NOCTCP, server->config.server_name,
                         client->nick, destination->nick);
            return COMMAND_KEEP_CLIENT;
        }
        ircv3_send_message(destination, client, "PRIVMSG", destination->nick, text);
        if (destination->away[0] != '\0')
            client_sendf(client, RPL_AWAY, server->config.server_name,
                         client->nick, destination->nick, destination->away);
    }
    return COMMAND_KEEP_CLIENT;
}
