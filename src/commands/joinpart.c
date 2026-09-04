/** @file joinpart.c @brief IRC JOIN/PART with channel access-policy enforcement. */
#include "commands.h"
#include "channel_log.h"
#include "channel_policy.h"
#include "chanserv.h"
#include "config.h"
#include "ircv3.h"
#include "modes.h"
#include "nospoof.h"
#include "numerics.h"

#include <stdio.h>
#include <string.h>

static void send_no_such_channel_query(Server *server, Client *client,
                                       const char *query) {
    int base_length;
    size_t length;
    if (server == NULL || client == NULL) return;
    if (query == NULL) query = "*";
    base_length = snprintf(NULL, 0, ":%s 403 %s  :No such channel",
                           server->config.server_name, client->nick);
    if (base_length < 0 || (size_t)base_length > IRC_LINE_CONTENT_MAX) return;
    length = strlen(query);
    if (length > IRC_LINE_CONTENT_MAX - (size_t)base_length)
        length = IRC_LINE_CONTENT_MAX - (size_t)base_length;
    client_sendf(client, ":%s 403 %s %.*s :No such channel",
                 server->config.server_name, client->nick,
                 (int)length, query);
}

static void join_one(Server *server, Client *client, const char *name,
                     const char *key, unsigned int redirect_depth) {
    Channel *channel;
    ChannelPrivilegeSet service_privileges;
    int first_member;
    int explicitly_invited;
    int owner_account;
    int protected_account;
    int banned;

    if (!channel_name_valid(name)) {
        send_no_such_channel_query(server, client, name);
        return;
    }
    if (client->channel_count >= IRC_MAX_CHANNELS_PER_CLIENT) {
        client_sendf(client, ERR_TOOMANYCHANNELS,
                     server->config.server_name, client->nick, name);
        return;
    }

    channel = hash_get(&server->channels_by_name, name);
    if (channel == NULL && server->channel_count >= server->config.max_channels) {
        client_sendf(client,
                     ":%s NOTICE %s :Cannot create %s: server channel limit reached (%zu)",
                     server->config.server_name, client->nick, name,
                     server->config.max_channels);
        snotice_broadcast(server, SNOTICE_FLOOD,
                          "Channel creation rejected for %s (nick=%s): global channel limit reached (%zu/%zu)",
                          name, client->nick, server->channel_count,
                          server->config.max_channels);
        return;
    }
    if (channel == NULL) channel = server_get_or_create_channel(server, name);
    if (channel == NULL || channel_has_client(channel, client)) return;

    chanserv_restore_channel(server, channel);
    service_privileges = chanserv_client_privileges(server, client, channel->name);
    owner_account = channel_privilege_has(service_privileges, CHANNEL_PRIV_OWNER);
    protected_account = channel_privilege_has(service_privileges, CHANNEL_PRIV_PROTECTED);
    explicitly_invited = channel_invite_has(channel, client->id);

    if (channel->key[0] != '\0' && (key == NULL || strcmp(channel->key, key) != 0)) {
        client_sendf(client, ERR_BADCHANNELKEY,
                     server->config.server_name, client->nick, channel->name);
        return;
    }

    banned = owner_account
                 ? 0
                 : protected_account
                       ? channel_client_is_banned_protected(channel, client)
                       : channel_client_is_banned(channel, client);
    if (banned) {
        if (channel->ban_redirect[0] != '\0' &&
            redirect_depth < IRC_JOIN_REDIRECT_MAX &&
            channel_name_valid(channel->ban_redirect)) {
            client_sendf(client, ERR_LINKCHANNEL,
                         server->config.server_name, client->nick,
                         channel->name, channel->ban_redirect);
            join_one(server, client, channel->ban_redirect, NULL,
                     redirect_depth + 1U);
            return;
        }
        client_sendf(client, ERR_BANNEDFROMCHAN,
                     server->config.server_name, client->nick, channel->name);
        return;
    }

    if (channel->user_limit != 0U && channel->member_count >= channel->user_limit) {
        if (channel->limit_redirect[0] != '\0' &&
            redirect_depth < IRC_JOIN_REDIRECT_MAX &&
            channel_name_valid(channel->limit_redirect)) {
            client_sendf(client, ERR_LINKCHANNEL,
                         server->config.server_name, client->nick,
                         channel->name, channel->limit_redirect);
            join_one(server, client, channel->limit_redirect, NULL,
                     redirect_depth + 1U);
            return;
        }
        client_sendf(client, ERR_CHANNELISFULL,
                     server->config.server_name, client->nick, channel->name);
        return;
    }

    if (channel_mode_has(channel->modes, CHANNEL_MODE_REGONLY_JOIN) &&
        !client_mode_has(client->modes, CLIENT_MODE_REGISTERED)) {
        client_sendf(client, ERR_NEEDREGGEDNICK,
                     server->config.server_name, client->nick, channel->name);
        return;
    }
    if (channel_mode_has(channel->modes, CHANNEL_MODE_OPER_ONLY) &&
        !client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_520,
                     server->config.server_name, client->nick, channel->name);
        return;
    }
    if (channel_mode_has(channel->modes, CHANNEL_MODE_ADMIN_ONLY) &&
        !client_mode_has(client->modes, CLIENT_MODE_NETADMIN)) {
        client_sendf(client, ERR_519,
                     server->config.server_name, client->nick, channel->name);
        return;
    }
    if (channel_mode_has(channel->modes, CHANNEL_MODE_SECURE_ONLY) &&
        !client_mode_has(client->modes, CLIENT_MODE_SECURE)) {
        client_sendf(client, ERR_SECUREONLYCHAN,
                     server->config.server_name, client->nick, channel->name);
        return;
    }
    if (channel_mode_has(channel->modes, CHANNEL_MODE_INVITE_ONLY) &&
        !explicitly_invited && !channel_client_is_invex(channel, client)) {
        client_sendf(client, ERR_INVITEONLYCHAN,
                     server->config.server_name, client->nick, channel->name);
        return;
    }
    if (!channel_join_throttle_allows(channel, client->id)) {
        client_sendf(client, ERR_TOOMANYJOINS,
                     server->config.server_name, client->nick, channel->name);
        return;
    }

    first_member = channel->member_count == 0U;
    if (channel_add_client(channel, client) < 0) return;
    channel_join_throttle_record(channel, client->id);
    if (explicitly_invited) (void)channel_invite_consume(channel, client->id);

    if (first_member && !channel_mode_has(channel->modes, CHANNEL_MODE_REGISTERED))
        (void)channel_add_privileges(channel, client,
                                     CHANNEL_PRIV_OWNER | CHANNEL_PRIV_OPERATOR);
    if (channel_mode_has(channel->modes, CHANNEL_MODE_REGISTERED) &&
        service_privileges != 0U)
        (void)channel_set_service_privileges(channel, client, service_privileges);

    ircv3_broadcast_join(channel, client);
    ircv3_away_notify_join(channel, client);
    channel_log_join(server, channel, client);

    if (channel->topic[0] == '\0') {
        client_sendf(client, RPL_NOTOPIC,
                     server->config.server_name, client->nick, channel->name);
    } else {
        client_sendf(client, RPL_TOPIC,
                     server->config.server_name, client->nick,
                     channel->name, channel->topic);
        client_sendf(client, RPL_TOPICWHOTIME,
                     server->config.server_name, client->nick,
                     channel->name, channel->topic_setter,
                     (unsigned long)channel->topic_time);
    }
    command_send_names(server, channel, client);
}

CommandResult command_join(Server *server, Client *client, char *params) {
    char *name;
    char *key;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "JOIN");
        return COMMAND_KEEP_CLIENT;
    }
    if (nospoof_version_restricted(server, client)) {
        client_sendf(client,
                     ":%s NOTICE %s :You must respond to the CTCP VERSION request before joining channels.",
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    name = strtok(params, " ");
    key = strtok(NULL, " ");
    join_one(server, client, name, key, 0U);
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_part(Server *server, Client *client, char *params) {
    char *name;
    char *reason;
    const char *delivered_reason;
    Channel *channel;
    char message[IRCD_MESSAGE_BUFFER_SIZE];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS,
                     server->config.server_name, client->nick, "PART");
        return COMMAND_KEEP_CLIENT;
    }

    name = strtok(params, " ");
    reason = strtok(NULL, "");
    if (reason != NULL && *reason == ':') ++reason;

    if (!channel_name_valid(name)) {
        send_no_such_channel_query(server, client, name);
        return COMMAND_KEEP_CLIENT;
    }
    channel = hash_get(&server->channels_by_name, name);
    if (channel == NULL) {
        client_sendf(client, ERR_NOSUCHCHANNEL,
                     server->config.server_name, client->nick, name);
        return COMMAND_KEEP_CLIENT;
    }
    if (!channel_has_client(channel, client)) {
        client_sendf(client, ERR_NOTONCHANNEL,
                     server->config.server_name, client->nick, channel->name);
        return COMMAND_KEEP_CLIENT;
    }

    delivered_reason = reason != NULL && *reason != '\0'
                           ? reason : IRC_DEFAULT_PART_REASON;
    if (!ircv3_message_wire_fits(client, "PART", channel->name,
                                 delivered_reason))
        delivered_reason = IRC_DEFAULT_PART_REASON;

    channel_log_part(server, channel, client, delivered_reason);
    (void)snprintf(message, sizeof(message), ":%s!%s@%s PART %s :%s\r\n",
                   client->nick, client->user, client->display_host,
                   channel->name, delivered_reason);
    channel_broadcast(channel, NULL, message);
    channel_remove_client(channel, client);
    server_remove_channel_if_empty(server, channel);
    return COMMAND_KEEP_CLIENT;
}
