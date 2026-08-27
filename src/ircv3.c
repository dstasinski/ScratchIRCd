/**
 * @file ircv3.c
 * @brief Shared IRCv3 capability-dependent protocol behavior.
 */

#include "ircv3.h"
#include "channel.h"
#include "usermode_policy.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/** Return true when recipient already appeared in a channel visited before stop. */
static int seen_in_earlier_channel(const Client *source,
                                   const ClientChannelLink *stop,
                                   const Client *recipient) {
    const ClientChannelLink *link;
    for (link = source->channels; link != NULL && link != stop; link = link->next)
        if (channel_has_client(link->channel, recipient)) return 1;
    return 0;
}

static void current_timestamp(char *out, size_t out_size) {
    struct timespec now;
    struct tm utc;
    long millis = 0L;
    time_t seconds;

    if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
        seconds = now.tv_sec;
        millis = now.tv_nsec / 1000000L;
    } else {
        seconds = time(NULL);
    }
    if (gmtime_r(&seconds, &utc) == NULL) {
        (void)snprintf(out, out_size, "1970-01-01T00:00:00.000Z");
        return;
    }
    (void)snprintf(out, out_size,
                   "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
                   utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                   utc.tm_hour, utc.tm_min, utc.tm_sec, millis);
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
    char timestamp[40];
    if (recipient == NULL || source == NULL || command == NULL ||
        target == NULL || text == NULL ||
        !ircv3_message_wire_fits(source, command, target, text)) return;

    if ((recipient->capabilities & CLIENT_CAP_SERVER_TIME) != 0U) {
        current_timestamp(timestamp, sizeof(timestamp));
        client_sendf(recipient, "@time=%s :%s!%s@%s %s %s :%s",
                     timestamp, source->nick, source->user, source->display_host,
                     command, target, text);
    } else {
        client_sendf(recipient, ":%s!%s@%s %s %s :%s",
                     source->nick, source->user, source->display_host,
                     command, target, text);
    }
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
