/**
 * @file mode.c
 * @brief Implementation of IRC MODE for users and channels.
 *
 * MODE is deliberately kept in the command layer. The core mode subsystem
 * (modes.h/modes.c) owns representation only, while this file owns parsing,
 * permissions, numeric replies, parameter consumption, and broadcasts.
 *
 * Channel membership privileges are +q owner, +a protected, +o operator,
 * +h halfop, and +v voice. Only protected members or owners may grant or
 * remove +a. This prevents an ordinary operator from stripping protection
 * immediately before attempting a kick or ban.
 */

#include "commands.h"
#include "channel_policy.h"
#include "config.h"
#include "modes.h"
#include "numerics.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ClientModeSet client_mode_bit(char letter) {
    switch (letter) {
        case 'B': return CLIENT_MODE_BOT;
        case 'd': return CLIENT_MODE_DEAF;
        case 'g': return CLIENT_MODE_GLOBALS;
        case 'H': return CLIENT_MODE_HIDE_OPER;
        case 'h': return CLIENT_MODE_HELPOP;
        case 'I': return CLIENT_MODE_HIDE_IDLE;
        case 'i': return CLIENT_MODE_INVISIBLE;
        case 'N': return CLIENT_MODE_NETADMIN;
        case 'o': return CLIENT_MODE_OPER;
        case 'p': return CLIENT_MODE_PRIVATE;
        case 'R': return CLIENT_MODE_REGONLY_MSG;
        case 'r': return CLIENT_MODE_REGISTERED;
        case 'S': return CLIENT_MODE_SERVICE;
        case 's': return CLIENT_MODE_SERVER_NOTICES;
        case 'T': return CLIENT_MODE_NO_CTCP;
        case 't': return CLIENT_MODE_VHOST;
        case 'V': return CLIENT_MODE_WEBIRC;
        case 'W': return CLIENT_MODE_WHOIS_NOTICE;
        case 'w': return CLIENT_MODE_WALLOPS;
        case 'x': return CLIENT_MODE_CLOAKED;
        case 'z': return CLIENT_MODE_SECURE;
        default:  return 0U;
    }
}

static int client_mode_self_settable(char letter) {
    switch (letter) {
        case 'B':
        case 'd':
        case 'g':
        case 'i':
        case 'p':
        case 'R':
        case 's':
        case 'T':
        case 'w':
            return 1;
        default:
            return 0;
    }
}

static void format_client_modes(const Client *client, char *out, size_t out_size) {
    static const char letters[] = "BdghHiNopRrSsTtVWwxz";
    size_t used = 0U;
    size_t i;

    if (out == NULL || out_size == 0U) return;
    out[used++] = '+';
    for (i = 0U; letters[i] != '\0' && used + 1U < out_size; ++i) {
        ClientModeSet bit = client_mode_bit(letters[i]);
        if (bit != 0U && client_mode_has(client->modes, bit)) out[used++] = letters[i];
    }
    out[used] = '\0';
}

static ChannelModeSet channel_boolean_mode_bit(char letter) {
    switch (letter) {
        case 'A': return CHANNEL_MODE_ADMIN_ONLY;
        case 'c': return CHANNEL_MODE_NO_COLOR;
        case 'i': return CHANNEL_MODE_INVITE_ONLY;
        case 'K': return CHANNEL_MODE_NO_KNOCK;
        case 'M': return CHANNEL_MODE_REGONLY_SPEAK;
        case 'm': return CHANNEL_MODE_MODERATED;
        case 'n': return CHANNEL_MODE_NO_EXTERNAL;
        case 'O': return CHANNEL_MODE_OPER_ONLY;
        case 'p': return CHANNEL_MODE_PRIVATE;
        case 'r': return CHANNEL_MODE_REGISTERED;
        case 'R': return CHANNEL_MODE_REGONLY_JOIN;
        case 'S': return CHANNEL_MODE_STRIP_COLOR;
        case 's': return CHANNEL_MODE_SECRET;
        case 't': return CHANNEL_MODE_TOPIC_LOCK;
        case 'T': return CHANNEL_MODE_NO_NOTICE;
        case 'V': return CHANNEL_MODE_NO_INVITE;
        case 'z': return CHANNEL_MODE_SECURE_ONLY;
        default:  return 0U;
    }
}

static ChannelPrivilegeSet channel_privilege_bit(char letter) {
    switch (letter) {
        case 'q': return CHANNEL_PRIV_OWNER;
        case 'a': return CHANNEL_PRIV_PROTECTED;
        case 'o': return CHANNEL_PRIV_OPERATOR;
        case 'h': return CHANNEL_PRIV_HALFOP;
        case 'v': return CHANNEL_PRIV_VOICE;
        default:  return 0U;
    }
}

static ChannelMember *actor_membership(const Channel *channel, const Client *actor) {
    return channel_find_member(channel, actor);
}

static int may_change_channel(const Channel *channel, const Client *actor) {
    ChannelMember *member = actor_membership(channel, actor);
    if (member == NULL) return 0;
    return channel_privilege_has(member->privileges,
                                 CHANNEL_PRIV_OWNER |
                                 CHANNEL_PRIV_PROTECTED |
                                 CHANNEL_PRIV_OPERATOR);
}

static int may_manage_protected(const Channel *channel, const Client *actor) {
    ChannelMember *member = actor_membership(channel, actor);
    return member != NULL &&
           channel_privilege_has(member->privileges,
                                 CHANNEL_PRIV_OWNER | CHANNEL_PRIV_PROTECTED);
}

/** Return true when a ban mask matches any currently protected non-owner member. */
static int ban_mask_targets_protected(const Channel *channel, const char *mask) {
    ChannelMember *member;
    char identity[IRCD_MESSAGE_BUFFER_SIZE];

    if (channel == NULL || mask == NULL) return 0;
    for (member = channel->members; member != NULL; member = member->next) {
        if (!channel_privilege_has(member->privileges, CHANNEL_PRIV_PROTECTED) ||
            channel_privilege_has(member->privileges, CHANNEL_PRIV_OWNER)) {
            continue;
        }
        (void)snprintf(identity, sizeof(identity), "%s!%s@%s",
                       member->client->nick, member->client->user,
                       member->client->display_host);
        if (irc_mask_match(mask, identity)) return 1;
    }
    return 0;
}

static int parse_uint(const char *text, unsigned long *value) {
    char *end = NULL;
    unsigned long parsed;
    if (text == NULL || *text == '\0' || value == NULL) return -1;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return -1;
    *value = parsed;
    return 0;
}

static void append_mode(char *buffer, size_t size, size_t *used,
                        char *last_sign, char sign, char mode) {
    if (*last_sign != sign && *used + 1U < size) {
        buffer[(*used)++] = sign;
        *last_sign = sign;
    }
    if (*used + 1U < size) buffer[(*used)++] = mode;
    buffer[*used] = '\0';
}

static void append_param(char *buffer, size_t size, const char *param) {
    size_t used;
    if (buffer == NULL || size == 0U || param == NULL) return;
    used = strlen(buffer);
    (void)snprintf(buffer + used, size - used, "%s%s",
                   used != 0U ? " " : "", param);
}

static void send_mask_list(Server *server, Client *client, Channel *channel,
                           char type) {
    ChannelMaskEntry *entry = NULL;
    if (type == 'b') {
        for (entry = channel->ban_list; entry != NULL; entry = entry->next)
            client_sendf(client, RPL_BANLIST, server->config.server_name,
                         client->nick, channel->name, entry->mask, "*", 0UL);
        client_sendf(client, RPL_ENDOFBANLIST, server->config.server_name,
                     client->nick, channel->name);
    } else if (type == 'e') {
        for (entry = channel->exception_list; entry != NULL; entry = entry->next)
            client_sendf(client, RPL_EXLIST, server->config.server_name,
                         client->nick, channel->name, entry->mask, "*", 0UL);
        client_sendf(client, RPL_ENDOFEXLIST, server->config.server_name,
                     client->nick, channel->name);
    } else if (type == 'I') {
        for (entry = channel->invite_exception_list; entry != NULL; entry = entry->next)
            client_sendf(client, RPL_INVEXLIST, server->config.server_name,
                         client->nick, channel->name, entry->mask, "*", 0UL);
        client_sendf(client, RPL_ENDOFINVEXLIST, server->config.server_name,
                     client->nick, channel->name);
    }
}

static void send_channel_modes(Server *server, Client *client, Channel *channel) {
    static const char booleans[] = "AciKMmnOprRSstTVz";
    char modes[128] = "+";
    char params[IRCD_MESSAGE_BUFFER_SIZE] = "";
    size_t used = 1U;
    size_t i;
    char number[64];

    for (i = 0U; booleans[i] != '\0' && used + 1U < sizeof(modes); ++i) {
        ChannelModeSet bit = channel_boolean_mode_bit(booleans[i]);
        if (bit != 0U && channel_mode_has(channel->modes, bit)) modes[used++] = booleans[i];
    }
    if (channel->key[0] != '\0' && used + 1U < sizeof(modes)) {
        modes[used++] = 'k'; append_param(params, sizeof(params), channel->key);
    }
    if (channel->user_limit != 0U && used + 1U < sizeof(modes)) {
        modes[used++] = 'l';
        (void)snprintf(number, sizeof(number), "%zu", channel->user_limit);
        append_param(params, sizeof(params), number);
    }
    if (channel->join_throttle_count != 0U && used + 1U < sizeof(modes)) {
        modes[used++] = 'j';
        (void)snprintf(number, sizeof(number), "%u:%u",
                       channel->join_throttle_count, channel->join_throttle_seconds);
        append_param(params, sizeof(params), number);
    }
    if (channel->limit_redirect[0] != '\0' && used + 1U < sizeof(modes)) {
        modes[used++] = 'L'; append_param(params, sizeof(params), channel->limit_redirect);
    }
    if (channel->ban_redirect[0] != '\0' && used + 1U < sizeof(modes)) {
        modes[used++] = 'B'; append_param(params, sizeof(params), channel->ban_redirect);
    }
    modes[used] = '\0';
    client_sendf(client, RPL_CHANNELMODEIS, server->config.server_name,
                 client->nick, channel->name, modes,
                 params[0] != '\0' ? params : "");
}

static CommandResult mode_user(Server *server, Client *client,
                               const char *target, char *mode_string) {
    Client *target_client = hash_get(&server->clients_by_nick, target);
    char formatted[64];
    char sign = '+';
    size_t i;

    if (target_client == NULL) {
        client_sendf(client, ERR_NOSUCHNICK, server->config.server_name,
                     client->nick, target);
        return COMMAND_KEEP_CLIENT;
    }
    if (target_client != client) {
        client_sendf(client, ERR_USERSDONTMATCH, server->config.server_name,
                     client->nick);
        return COMMAND_KEEP_CLIENT;
    }
    if (mode_string == NULL || *mode_string == '\0') {
        format_client_modes(client, formatted, sizeof(formatted));
        client_sendf(client, RPL_UMODEIS, server->config.server_name,
                     client->nick, formatted);
        return COMMAND_KEEP_CLIENT;
    }

    for (i = 0U; mode_string[i] != '\0'; ++i) {
        char letter = mode_string[i];
        ClientModeSet bit;
        if (letter == '+' || letter == '-') { sign = letter; continue; }
        bit = client_mode_bit(letter);
        if (bit == 0U) {
            client_sendf(client, ERR_UMODEUNKNOWNFLAG,
                         server->config.server_name, client->nick);
            continue;
        }
        if (!client_mode_self_settable(letter)) {
            client_sendf(client, ERR_NOPRIVILEGES,
                         server->config.server_name, client->nick);
            continue;
        }
        if (sign == '+') client->modes = client_mode_add(client->modes, bit);
        else client->modes = client_mode_remove(client->modes, bit);
    }
    format_client_modes(client, formatted, sizeof(formatted));
    client_sendf(client, RPL_UMODEIS, server->config.server_name,
                 client->nick, formatted);
    return COMMAND_KEEP_CLIENT;
}

static CommandResult mode_channel(Server *server, Client *client,
                                  const char *target, char *mode_string,
                                  char **argv, size_t argc) {
    Channel *channel = hash_get(&server->channels_by_name, target);
    char changed[128] = "";
    char changed_params[IRCD_MESSAGE_BUFFER_SIZE] = "";
    char last_sign = '\0';
    char sign = '+';
    size_t used = 0U;
    size_t argi = 0U;
    size_t i;
    int may_change;

    if (channel == NULL) {
        client_sendf(client, ERR_NOSUCHCHANNEL, server->config.server_name,
                     client->nick, target);
        return COMMAND_KEEP_CLIENT;
    }
    if (mode_string == NULL || *mode_string == '\0') {
        send_channel_modes(server, client, channel);
        return COMMAND_KEEP_CLIENT;
    }

    may_change = may_change_channel(channel, client);
    for (i = 0U; mode_string[i] != '\0'; ++i) {
        char letter = mode_string[i];
        ChannelModeSet boolean_bit;
        ChannelPrivilegeSet privilege_bit;
        const char *param = argi < argc ? argv[argi] : NULL;

        if (letter == '+' || letter == '-') { sign = letter; continue; }
        if ((letter == 'b' || letter == 'e' || letter == 'I') && param == NULL) {
            send_mask_list(server, client, channel, letter);
            continue;
        }
        if (!may_change) {
            client_sendf(client, ERR_CHANOPRIVSNEEDED,
                         server->config.server_name, client->nick, channel->name);
            return COMMAND_KEEP_CLIENT;
        }

        boolean_bit = channel_boolean_mode_bit(letter);
        if (boolean_bit != 0U) {
            if (letter == 'r') {
                client_sendf(client, ERR_ONLYSERVERSCANCHANGE,
                             server->config.server_name, client->nick,
                             channel->name);
                continue;
            }
            if (sign == '+') channel->modes = channel_mode_add(channel->modes, boolean_bit);
            else channel->modes = channel_mode_remove(channel->modes, boolean_bit);
            append_mode(changed, sizeof(changed), &used, &last_sign, sign, letter);
            continue;
        }

        privilege_bit = channel_privilege_bit(letter);
        if (privilege_bit != 0U) {
            Client *subject;
            if (param == NULL) {
                client_sendf(client, ERR_NEEDMOREPARAMS,
                             server->config.server_name, client->nick, "MODE");
                continue;
            }
            ++argi;
            subject = hash_get(&server->clients_by_nick, param);
            if (subject == NULL) {
                client_sendf(client, ERR_NOSUCHNICK, server->config.server_name,
                             client->nick, param);
                continue;
            }
            if (!channel_has_client(channel, subject)) {
                client_sendf(client, ERR_USERNOTINCHANNEL,
                             server->config.server_name, client->nick,
                             subject->nick, channel->name);
                continue;
            }
            if (letter == 'a' && !may_manage_protected(channel, client)) {
                client_sendf(client, ERR_CHANOPRIVSNEEDED,
                             server->config.server_name, client->nick, channel->name);
                continue;
            }
            if (sign == '+') (void)channel_add_privileges(channel, subject, privilege_bit);
            else (void)channel_remove_privileges(channel, subject, privilege_bit);
            append_mode(changed, sizeof(changed), &used, &last_sign, sign, letter);
            append_param(changed_params, sizeof(changed_params), subject->nick);
            continue;
        }

        if (letter == 'k') {
            if (sign == '+') {
                if (param == NULL) {
                    client_sendf(client, ERR_NEEDMOREPARAMS,
                                 server->config.server_name, client->nick, "MODE");
                    continue;
                }
                ++argi;
                (void)snprintf(channel->key, sizeof(channel->key), "%s", param);
                append_param(changed_params, sizeof(changed_params), param);
            } else {
                channel->key[0] = '\0';
                if (param != NULL) { ++argi; append_param(changed_params, sizeof(changed_params), param); }
            }
            append_mode(changed, sizeof(changed), &used, &last_sign, sign, letter);
            continue;
        }

        if (letter == 'l') {
            if (sign == '+') {
                unsigned long value;
                if (param == NULL || parse_uint(param, &value) != 0 || value == 0UL) {
                    client_sendf(client, ERR_NEEDMOREPARAMS,
                                 server->config.server_name, client->nick, "MODE");
                    if (param != NULL) ++argi;
                    continue;
                }
                ++argi;
                channel->user_limit = (size_t)value;
                append_param(changed_params, sizeof(changed_params), param);
            } else channel->user_limit = 0U;
            append_mode(changed, sizeof(changed), &used, &last_sign, sign, letter);
            continue;
        }

        if (letter == 'j') {
            if (sign == '+') {
                char copy[64];
                char *colon;
                unsigned long count;
                unsigned long seconds;
                if (param == NULL || strlen(param) >= sizeof(copy)) {
                    client_sendf(client, ERR_NEEDMOREPARAMS,
                                 server->config.server_name, client->nick, "MODE");
                    continue;
                }
                ++argi;
                (void)snprintf(copy, sizeof(copy), "%s", param);
                colon = strchr(copy, ':');
                if (colon == NULL) {
                    client_sendf(client, ERR_NEEDMOREPARAMS,
                                 server->config.server_name, client->nick, "MODE");
                    continue;
                }
                *colon++ = '\0';
                if (parse_uint(copy, &count) != 0 || parse_uint(colon, &seconds) != 0 ||
                    count == 0UL || seconds == 0UL || count > UINT_MAX || seconds > UINT_MAX) {
                    client_sendf(client, ERR_NEEDMOREPARAMS,
                                 server->config.server_name, client->nick, "MODE");
                    continue;
                }
                channel->join_throttle_count = (unsigned int)count;
                channel->join_throttle_seconds = (unsigned int)seconds;
                append_param(changed_params, sizeof(changed_params), param);
            } else {
                channel->join_throttle_count = 0U;
                channel->join_throttle_seconds = 0U;
            }
            append_mode(changed, sizeof(changed), &used, &last_sign, sign, letter);
            continue;
        }

        if (letter == 'L' || letter == 'B') {
            char *field = letter == 'L' ? channel->limit_redirect : channel->ban_redirect;
            size_t field_size = letter == 'L' ? sizeof(channel->limit_redirect)
                                               : sizeof(channel->ban_redirect);
            if (sign == '+') {
                if (param == NULL || strchr(IRC_CHANNEL_PREFIXES, param[0]) == NULL) {
                    client_sendf(client, ERR_NEEDMOREPARAMS,
                                 server->config.server_name, client->nick, "MODE");
                    continue;
                }
                ++argi;
                (void)snprintf(field, field_size, "%s", param);
                append_param(changed_params, sizeof(changed_params), param);
            } else field[0] = '\0';
            append_mode(changed, sizeof(changed), &used, &last_sign, sign, letter);
            continue;
        }

        if (letter == 'b' || letter == 'e' || letter == 'I') {
            ChannelMaskEntry **list;
            if (param == NULL) continue;
            ++argi;
            if (letter == 'b' && sign == '+' &&
                ban_mask_targets_protected(channel, param) &&
                !may_manage_protected(channel, client)) {
                client_sendf(client, ERR_CHANOPRIVSNEEDED,
                             server->config.server_name, client->nick, channel->name);
                continue;
            }
            list = letter == 'b' ? &channel->ban_list
                 : letter == 'e' ? &channel->exception_list
                                 : &channel->invite_exception_list;
            if (sign == '+')
                (void)channel_mask_add_authorized(
                    list, param,
                    letter == 'b' && may_manage_protected(channel, client));
            else
                (void)channel_mask_remove(list, param);
            append_mode(changed, sizeof(changed), &used, &last_sign, sign, letter);
            append_param(changed_params, sizeof(changed_params), param);
            continue;
        }

        client_sendf(client, ERR_UNKNOWNMODE, server->config.server_name,
                     client->nick, letter);
    }

    if (changed[0] != '\0') {
        char message[IRCD_MESSAGE_BUFFER_SIZE];
        (void)snprintf(message, sizeof(message), ":%s!%s@%s MODE %s %s%s%s\r\n",
                       client->nick, client->user, client->display_host,
                       channel->name, changed,
                       changed_params[0] != '\0' ? " " : "",
                       changed_params);
        channel_broadcast(channel, NULL, message);
    }
    return COMMAND_KEEP_CLIENT;
}

CommandResult command_mode(Server *server, Client *client, char *params) {
    char *target;
    char *mode_string;
    char *argv[IRC_MODE_MAX_PARAMS];
    size_t argc = 0U;
    char *token;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "MODE");
        return COMMAND_KEEP_CLIENT;
    }

    target = strtok(params, " ");
    mode_string = strtok(NULL, " ");
    while (argc < IRC_MODE_MAX_PARAMS && (token = strtok(NULL, " ")) != NULL)
        argv[argc++] = token;

    if (target == NULL || *target == '\0') {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "MODE");
        return COMMAND_KEEP_CLIENT;
    }

    if (strchr(IRC_CHANNEL_PREFIXES, target[0]) != NULL)
        return mode_channel(server, client, target, mode_string, argv, argc);
    return mode_user(server, client, target, mode_string);
}
