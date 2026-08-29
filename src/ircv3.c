/**
 * @file ircv3.c
 * @brief Shared IRCv3 capability-dependent protocol behavior.
 */

#include "ircv3.h"
#include "channel.h"
#include "server.h"
#include "usermode_policy.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define IRCV3_TAG_TOKEN_MAX 2048U
#define IRCV3_TAG_SLOT_COUNT 4096U
#define IRCV3_TAG_SLOT_EMPTY UINT16_MAX

/** Return true when recipient already appeared in a channel visited before stop. */
static int seen_in_earlier_channel(const Client *source,
                                   const ClientChannelLink *stop,
                                   const Client *recipient) {
    const ClientChannelLink *link;
    for (link = source->channels; link != NULL && link != stop; link = link->next)
        if (channel_has_client(link->channel, recipient)) return 1;
    return 0;
}

static size_t decoded_tag_value_length(const char *value) {
    size_t length = 0U;
    const unsigned char *cursor = (const unsigned char *)value;
    while (cursor != NULL && *cursor != '\0') {
        if (*cursor == '\\') {
            ++cursor;
            if (*cursor == '\0') break;
        }
        ++length;
        ++cursor;
    }
    return length;
}

static size_t tag_key_hash(const char *key, size_t length) {
    size_t hash = 2166136261U;
    size_t index;
    for (index = 0U; index < length; ++index) {
        hash ^= (unsigned char)key[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t tag_slot_for(char *const *tokens, const uint16_t *key_lengths,
                           const uint16_t *slots, size_t token_index) {
    size_t slot = tag_key_hash(tokens[token_index], key_lengths[token_index]) &
                  (IRCV3_TAG_SLOT_COUNT - 1U);
    while (slots[slot] != IRCV3_TAG_SLOT_EMPTY) {
        size_t existing = slots[slot];
        if (key_lengths[existing] == key_lengths[token_index] &&
            memcmp(tokens[existing], tokens[token_index],
                   key_lengths[token_index]) == 0)
            break;
        slot = (slot + 1U) & (IRCV3_TAG_SLOT_COUNT - 1U);
    }
    return slot;
}

void ircv3_begin_command(Server *server, Client *client, char *tags) {
    char *tokens[IRCV3_TAG_TOKEN_MAX];
    uint16_t key_lengths[IRCV3_TAG_TOKEN_MAX];
    uint16_t slots[IRCV3_TAG_SLOT_COUNT];
    char *save = NULL;
    char *token;
    char label[IRCV3_LABEL_ENCODED_MAX + 1U] = "";
    size_t token_count = 0U;
    size_t index;
    size_t used = 0U;

    if (client == NULL) return;
    client->ircv3_client_tags[0] = '\0';
    if (tags == NULL || *tags == '\0') return;

    memset(slots, 0xff, sizeof(slots));
    for (token = strtok_r(tags, ";", &save);
         token != NULL && token_count < IRCV3_TAG_TOKEN_MAX;
         token = strtok_r(NULL, ";", &save)) {
        char *equals = strchr(token, '=');
        size_t key_length = (size_t)((equals != NULL ? equals : token + strlen(token)) - token);

        tokens[token_count] = token;
        key_lengths[token_count] = (uint16_t)key_length;

        if (key_length == 5U && memcmp(token, "label", 5U) == 0) {
            label[0] = '\0';
            if (equals != NULL && equals[1] != '\0' &&
                strlen(equals + 1) <= IRCV3_LABEL_ENCODED_MAX &&
                decoded_tag_value_length(equals + 1) <= IRCV3_LABEL_MAX)
                (void)snprintf(label, sizeof(label), "%s", equals + 1);
        }

        if (token[0] == '+' &&
            (client->capabilities & CLIENT_CAP_MESSAGE_TAGS) != 0U) {
            size_t slot = tag_slot_for(tokens, key_lengths, slots, token_count);
            slots[slot] = (uint16_t)token_count;
        }
        ++token_count;
    }

    /* Only the final occurrence of each client-only key is relayed, keeping
     * server output well-formed without trusting or rewriting tag values. */
    for (index = 0U; index < token_count; ++index) {
        size_t slot;
        size_t token_length;
        size_t needed;
        if (tokens[index][0] != '+' ||
            (client->capabilities & CLIENT_CAP_MESSAGE_TAGS) == 0U)
            continue;
        slot = tag_slot_for(tokens, key_lengths, slots, index);
        if (slots[slot] != index) continue;
        token_length = strlen(tokens[index]);
        needed = token_length + (used != 0U ? 1U : 0U);
        if (needed <= IRCV3_CLIENT_TAG_DATA_MAX - used) {
            if (used != 0U) client->ircv3_client_tags[used++] = ';';
            memcpy(client->ircv3_client_tags + used, tokens[index], token_length);
            used += token_length;
            client->ircv3_client_tags[used] = '\0';
        }
    }

    if (server != NULL && label[0] != '\0' &&
        (client->capabilities & CLIENT_CAP_LABELED_RESPONSE) != 0U &&
        (client->capabilities & CLIENT_CAP_BATCH) != 0U)
        client_labeled_response_begin(client, server->config.server_name, label);
}

void ircv3_end_command(Client *client) {
    if (client == NULL) return;
    client_labeled_response_end(client);
    client->ircv3_client_tags[0] = '\0';
}

int ircv3_message_wire_fits(const Client *source, const char *command,
                            const char *target, const char *text) {
    size_t wire_len;
    if (source == NULL || command == NULL || target == NULL || text == NULL)
        return 0;

    /* Untagged relay form:
     *   :nick!user@host COMMAND target :text
     * IRCv3 tags have a separate allowance; the remainder still gets the
     * normal 510-byte content budget. Client identity fields and command/
     * target sizes are bounded protocol fields, so this arithmetic is safe. */
    wire_len = 1U + strlen(source->nick) + 1U + strlen(source->user) + 1U +
               strlen(source->display_host) + 1U + strlen(command) + 1U +
               strlen(target) + 2U + strlen(text);
    return wire_len <= IRC_LINE_CONTENT_MAX;
}

void ircv3_send_message(Client *recipient, const Client *source,
                        const char *command, const char *target,
                        const char *text) {
    if (recipient == NULL || source == NULL || command == NULL ||
        target == NULL || text == NULL ||
        !ircv3_message_wire_fits(source, command, target, text)) return;

    if (recipient == source) client_labeled_response_suppress(recipient, 1);
    if ((recipient->capabilities & CLIENT_CAP_MESSAGE_TAGS) != 0U &&
        source->ircv3_client_tags[0] != '\0') {
        client_sendf(recipient, "@%s :%s!%s@%s %s %s :%s",
                     source->ircv3_client_tags, source->nick, source->user,
                     source->display_host, command, target, text);
    } else {
        client_sendf(recipient, ":%s!%s@%s %s %s :%s",
                     source->nick, source->user, source->display_host,
                     command, target, text);
    }
    if (recipient == source) client_labeled_response_suppress(recipient, 0);
}

void ircv3_send_tagmsg(Client *recipient, const Client *source,
                       const char *target) {
    if (recipient == NULL || source == NULL || target == NULL ||
        (recipient->capabilities & CLIENT_CAP_MESSAGE_TAGS) == 0U ||
        source->ircv3_client_tags[0] == '\0') return;
    if (recipient == source) client_labeled_response_suppress(recipient, 1);
    client_sendf(recipient, "@%s :%s!%s@%s TAGMSG %s",
                 source->ircv3_client_tags, source->nick, source->user,
                 source->display_host, target);
    if (recipient == source) client_labeled_response_suppress(recipient, 0);
}

void ircv3_broadcast_tagmsg(Channel *channel, const Client *except,
                            const Client *source, const char *target) {
    ChannelMember *member;
    if (channel == NULL || source == NULL || target == NULL) return;
    for (member = channel->members; member != NULL; member = member->next) {
        if (member->client == except) continue;
        ircv3_send_tagmsg(member->client, source, target);
    }
}

void ircv3_broadcast_join(Channel *channel, const Client *client) {
    ChannelMember *member;
    const char *account;
    if (channel == NULL || client == NULL) return;
    account = client->account_name[0] != '\0' ? client->account_name : "*";
    for (member = channel->members; member != NULL; member = member->next) {
        Client *recipient = member->client;
        if ((recipient->capabilities & CLIENT_CAP_EXTENDED_JOIN) != 0U) {
            client_sendf(recipient, ":%s!%s@%s JOIN %s %s :%s",
                         client->nick, client->user, client->display_host,
                         channel->name, account, client->realname);
        } else {
            client_sendf(recipient, ":%s!%s@%s JOIN %s",
                         client->nick, client->user, client->display_host,
                         channel->name);
        }
    }
}

static void send_away_notify(Client *recipient, const Client *client) {
    if (recipient == NULL || client == NULL || recipient == client ||
        (recipient->capabilities & CLIENT_CAP_AWAY_NOTIFY) == 0U) return;
    if (client->away[0] != '\0')
        client_sendf(recipient, ":%s!%s@%s AWAY :%s",
                     client->nick, client->user, client->display_host,
                     client->away);
    else
        client_sendf(recipient, ":%s!%s@%s AWAY",
                     client->nick, client->user, client->display_host);
}

void ircv3_away_notify(Client *client) {
    ClientChannelLink *link;
    if (client == NULL || !client->registered) return;
    for (link = client->channels; link != NULL; link = link->next) {
        ChannelMember *member;
        for (member = link->channel->members; member != NULL; member = member->next) {
            Client *recipient = member->client;
            if (recipient == client || seen_in_earlier_channel(client, link, recipient))
                continue;
            send_away_notify(recipient, client);
        }
    }
}

void ircv3_away_notify_join(Channel *channel, const Client *client) {
    ChannelMember *member;
    if (channel == NULL || client == NULL || client->away[0] == '\0') return;
    for (member = channel->members; member != NULL; member = member->next)
        send_away_notify(member->client, client);
}

void ircv3_broadcast_message(Channel *channel, const Client *except,
                             const Client *source, const char *command,
                             const char *target, const char *text) {
    ChannelMember *member;
    if (channel == NULL ||
        !ircv3_message_wire_fits(source, command, target, text)) return;
    for (member = channel->members; member != NULL; member = member->next) {
        if (member->client == except) continue;
        /* +d suppresses ordinary channel PRIVMSGs, but command-prefixed text
         * (currently '!') remains visible so bot/command traffic still works. */
        if (strcmp(command, "PRIVMSG") == 0 &&
            !usermode_deaf_allows_text(member->client, text)) continue;
        ircv3_send_message(member->client, source, command, target, text);
    }
}

void ircv3_account_notify(Client *client) {
    ClientChannelLink *link;
    char message[IRCD_MESSAGE_BUFFER_SIZE];
    const char *account;

    if (client == NULL || !client->registered) return;
    account = client->account_name[0] != '\0' ? client->account_name : "*";
    (void)snprintf(message, sizeof(message), ":%s!%s@%s ACCOUNT %s",
                   client->nick, client->user, client->display_host, account);

    for (link = client->channels; link != NULL; link = link->next) {
        ChannelMember *member;
        for (member = link->channel->members; member != NULL; member = member->next) {
            Client *recipient = member->client;
            if (recipient == client ||
                (recipient->capabilities & CLIENT_CAP_ACCOUNT_NOTIFY) == 0U ||
                seen_in_earlier_channel(client, link, recipient)) continue;
            client_send_line(recipient, message);
        }
    }
}
