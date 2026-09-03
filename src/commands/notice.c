/**
 * @file notice.c
 * @brief Implementation of IRC NOTICE.
 *
 * NOTICE mirrors PRIVMSG restrictions without automatic error replies. Channel
 * +c silently rejects colored notices, while +S strips color before history and
 * delivery. Public prefixes always use display_host.
 */

#include "commands.h"
#include "channel_log.h"
#include "channel_policy.h"
#include "config.h"
#include "history_db.h"
#include "ircv3.h"
#include "message_policy.h"
#include "modes.h"
#include "nospoof.h"
#include "presence.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int is_ctcp(const char *text) {
    return text != NULL && text[0] == '\001';
}

static void store_channel_history(Server *server, Client *client,
                                  const char *target, const char *text) {
    HistoryDb *db;
    HistoryRecord record;
    struct timespec now;
    size_t text_length;

    text_length = strlen(text);
    if (text_length > IRCD_HISTORY_TEXT_MAX) return;
    memset(&record, 0, sizeof(record));
    (void)snprintf(record.target, sizeof(record.target), "%s", target);
    (void)snprintf(record.command, sizeof(record.command), "NOTICE");
    (void)snprintf(record.nick, sizeof(record.nick), "%s", client->nick);
    (void)snprintf(record.user, sizeof(record.user), "%s", client->user);
    (void)snprintf(record.host, sizeof(record.host), "%s", client->display_host);
    (void)snprintf(record.account, sizeof(record.account), "%s", client->account_name);
    memcpy(record.text, text, text_length + 1U);
    if (clock_gettime(CLOCK_REALTIME, &now) == 0)
        record.created_at_ms = (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    else
        record.created_at_ms = (int64_t)time(NULL) * 1000;

    db = history_db_shared(server->config.history_db);
    if (db != NULL && history_db_add(db, &record) == 0)
        (void)history_db_shared_maintain(server->config.history_db,
                                        server->config.history_retention_days,
                                        server->config.history_max_rows,
                                        record.created_at_ms);
}

CommandResult command_notice(Server *server, Client *client, char *params) {
    char *target;
    char *text;

    /* RFC NOTICE never produces an automatic error response, including when
     * received before registration. */
    if (client == NULL || !client->registered || params == NULL)
        return COMMAND_KEEP_CLIENT;

    /* CTCP metadata probes target the server identity rather than a Client.
     * Consume valid no-spoof replies before ordinary nickname routing. */
    if (nospoof_handle_notice(server, client, params))
        return COMMAND_KEEP_CLIENT;

    target = strtok(params, " ");
    text = strtok(NULL, "");
    if (target == NULL || text == NULL || *target == '\0' || strchr(target, ',') != NULL)
        return COMMAND_KEEP_CLIENT;
    if (*text == ':') ++text;
    if (*text == '\0') return COMMAND_KEEP_CLIENT;

    if (strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL) {
        Channel *channel = hash_get(&server->channels_by_name, target);
        ChannelMember *member;
        int banned;
        char stripped[IRCD_MESSAGE_BUFFER_SIZE];
        const char *delivered_text = text;

        if (channel == NULL || channel_mode_has(channel->modes, CHANNEL_MODE_NO_NOTICE))
            return COMMAND_KEEP_CLIENT;
        member = channel_find_member(channel, client);
        if (member != NULL && channel_privilege_has(member->privileges, CHANNEL_PRIV_OWNER))
            banned = 0;
        else if (member != NULL && channel_privilege_has(member->privileges, CHANNEL_PRIV_PROTECTED))
            banned = channel_client_is_banned_protected(channel, client);
        else
            banned = channel_client_is_banned(channel, client);
        if (banned) return COMMAND_KEEP_CLIENT;
        if (member == NULL && channel_mode_has(channel->modes, CHANNEL_MODE_NO_EXTERNAL))
            return COMMAND_KEEP_CLIENT;
        if (channel_mode_has(channel->modes, CHANNEL_MODE_REGONLY_SPEAK) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED))
            return COMMAND_KEEP_CLIENT;
        if (channel_mode_has(channel->modes, CHANNEL_MODE_MODERATED) &&
            (member == NULL || !channel_privilege_has(member->privileges,
                CHANNEL_PRIV_VOICE | CHANNEL_PRIV_HALFOP | CHANNEL_PRIV_OPERATOR |
                CHANNEL_PRIV_PROTECTED | CHANNEL_PRIV_OWNER))) return COMMAND_KEEP_CLIENT;
        if (channel_mode_has(channel->modes, CHANNEL_MODE_NO_COLOR) &&
            message_contains_color(text)) return COMMAND_KEEP_CLIENT;
        if (channel_mode_has(channel->modes, CHANNEL_MODE_STRIP_COLOR)) {
            message_strip_color(text, stripped, sizeof(stripped));
            delivered_text = stripped;
        }
        /* NOTICE must not generate an automatic error response. Silently
         * reject an otherwise valid inbound notice if adding its source prefix
         * would make the relayed form unrepresentable; do so before persistence
         * or idle-time accounting so storage reflects actual delivery. */
        if (!ircv3_message_wire_fits(client, "NOTICE", channel->name, delivered_text))
            return COMMAND_KEEP_CLIENT;

        store_channel_history(server, client, channel->name, delivered_text);
        channel_log_message(server, channel, client, delivered_text, 1);
        ircv3_broadcast_message(channel, client, client, "NOTICE",
                                channel->name, delivered_text);
    } else {
        Client *destination = hash_get(&server->clients_by_nick, target);
        if (destination == NULL) return COMMAND_KEEP_CLIENT;
        if (presence_silence_matches(destination, client)) return COMMAND_KEEP_CLIENT;
        if (client_mode_has(destination->modes, CLIENT_MODE_REGONLY_MSG) &&
            !client_mode_has(client->modes, CLIENT_MODE_REGISTERED)) return COMMAND_KEEP_CLIENT;
        if (client_mode_has(destination->modes, CLIENT_MODE_NO_CTCP) && is_ctcp(text))
            return COMMAND_KEEP_CLIENT;
        if (!ircv3_message_wire_fits(client, "NOTICE", destination->nick, text))
            return COMMAND_KEEP_CLIENT;
        ircv3_send_message(destination, client, "NOTICE", destination->nick, text);
    }

    return COMMAND_KEEP_CLIENT;
}
