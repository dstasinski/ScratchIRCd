/**
 * @file chathistory.c
 * @brief IRCv3 draft CHATHISTORY LATEST playback from SQLite.
 *
 * This initial implementation deliberately supports only channel LATEST
 * requests. The requester must currently be a member of the target channel.
 * `draft/chathistory` is required; `batch` and `server-time` enhance playback
 * when negotiated but are not independently required.
 */

#include "commands.h"
#include "history_db.h"
#include "numerics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static void format_timestamp(int64_t milliseconds, char *out, size_t out_size) {
    time_t seconds = (time_t)(milliseconds / 1000);
    struct tm utc;
    long millis = (long)(milliseconds % 1000);
    if (millis < 0) millis = 0;
    if (gmtime_r(&seconds, &utc) == NULL) {
        (void)snprintf(out, out_size, "1970-01-01T00:00:00.000Z");
        return;
    }
    (void)snprintf(out, out_size,
                   "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
                   utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                   utc.tm_hour, utc.tm_min, utc.tm_sec, millis);
}

CommandResult command_chathistory(Server *server, Client *client, char *params) {
    char *subcommand;
    char *target;
    char *reference;
    char *limit_text;
    char *end = NULL;
    unsigned long requested;
    size_t limit;
    Channel *channel;
    HistoryDb db = {0};
    HistoryRecord *records = NULL;
    size_t count = 0U;
    size_t i;
    int use_batch;
    char batch_id[IRCD_HISTORY_BATCH_ID_MAX + 1U];

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if ((client->capabilities & CLIENT_CAP_CHATHISTORY) == 0U) {
        client_sendf(client, ERR_UNKNOWNCOMMAND, server->config.server_name,
                     client->nick, "CHATHISTORY");
        return COMMAND_KEEP_CLIENT;
    }
    if (params == NULL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "CHATHISTORY");
        return COMMAND_KEEP_CLIENT;
    }

    subcommand = strtok(params, " ");
    target = strtok(NULL, " ");
    reference = strtok(NULL, " ");
    limit_text = strtok(NULL, " ");
    if (subcommand == NULL || target == NULL || reference == NULL ||
        limit_text == NULL || strtok(NULL, " ") != NULL ||
        strcasecmp(subcommand, "LATEST") != 0 || strcmp(reference, "*") != 0) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "CHATHISTORY LATEST <channel> * <limit>");
        return COMMAND_KEEP_CLIENT;
    }

    channel = hash_get(&server->channels_by_name, target);
    if (channel == NULL || !channel_has_client(channel, client)) {
        client_sendf(client, ERR_NOSUCHCHANNEL, server->config.server_name,
                     client->nick, target);
        return COMMAND_KEEP_CLIENT;
    }

    requested = strtoul(limit_text, &end, 10);
    if (end == limit_text || *end != '\0' || requested == 0UL) {
        client_sendf(client, ERR_NEEDMOREPARAMS, server->config.server_name,
                     client->nick, "CHATHISTORY LATEST <channel> * <limit>");
        return COMMAND_KEEP_CLIENT;
    }
    limit = (size_t)requested;
    if (limit > server->config.history_limit) limit = server->config.history_limit;
    if (limit > IRCD_HISTORY_HARD_LIMIT) limit = IRCD_HISTORY_HARD_LIMIT;

    records = calloc(limit, sizeof(*records));
    if (records == NULL || history_db_open(&db, server->config.history_db) != 0 ||
        history_db_latest(&db, channel->name, limit, records, limit, &count) != 0) {
        history_db_close(&db);
        free(records);
        client_sendf(client, ERR_FILEERROR, server->config.server_name,
                     client->nick, "reading", server->config.history_db);
        return COMMAND_KEEP_CLIENT;
    }
    history_db_close(&db);

    use_batch = (client->capabilities & CLIENT_CAP_BATCH) != 0U;
    if (use_batch) {
        (void)snprintf(batch_id, sizeof(batch_id), "h%llu%ld",
                       (unsigned long long)client->id, (long)time(NULL));
        client_sendf(client, ":%s BATCH +%s chathistory %s",
                     server->config.server_name, batch_id, channel->name);
    }

    for (i = 0U; i < count; ++i) {
        char timestamp[40];
        HistoryRecord *record = &records[i];
        int use_time = (client->capabilities & CLIENT_CAP_SERVER_TIME) != 0U;
        format_timestamp(record->created_at_ms, timestamp, sizeof(timestamp));

        if (use_batch && use_time) {
            client_sendf(client,
                         "@batch=%s;time=%s :%s!%s@%s %s %s :%s",
                         batch_id, timestamp, record->nick, record->user,
                         record->host, record->command, record->target, record->text);
        } else if (use_batch) {
            client_sendf(client,
                         "@batch=%s :%s!%s@%s %s %s :%s",
                         batch_id, record->nick, record->user, record->host,
                         record->command, record->target, record->text);
        } else if (use_time) {
            client_sendf(client,
                         "@time=%s :%s!%s@%s %s %s :%s",
                         timestamp, record->nick, record->user, record->host,
                         record->command, record->target, record->text);
        } else {
            client_sendf(client, ":%s!%s@%s %s %s :%s",
                         record->nick, record->user, record->host,
                         record->command, record->target, record->text);
        }
    }

    if (use_batch)
        client_sendf(client, ":%s BATCH -%s", server->config.server_name, batch_id);
    free(records);
    return COMMAND_KEEP_CLIENT;
}
