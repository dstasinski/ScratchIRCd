/**
 * @file channel_log.c
 * @brief Optional ChanServ-controlled durable batched per-channel logging.
 */

#include "channel_log.h"
#include "chanserv_db.h"
#include "modes.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

typedef struct ChannelLogState {
    char channel[IRC_CHANNEL_NAME_MAX + 1U];
    int known;
    int enabled;
    char date_suffix[32];
    struct ChannelLogState *next;
} ChannelLogState;

static ChannelLogState *states;

static ChannelLogState *state_for(const char *channel_name, int create) {
    ChannelLogState *state;
    for (state = states; state != NULL; state = state->next)
        if (strcasecmp(state->channel, channel_name) == 0) return state;
    if (!create) return NULL;
    state = calloc(1U, sizeof(*state));
    if (state == NULL) return NULL;
    (void)snprintf(state->channel, sizeof(state->channel), "%s", channel_name);
    state->next = states;
    states = state;
    return state;
}

static int db_get_enabled(Server *server, const char *channel_name,
                          int *registered, int *enabled) {
    ChanServDb db = {0};
    int rc;
    if (registered != NULL) *registered = 0;
    if (enabled != NULL) *enabled = 0;
    if (server == NULL || channel_name == NULL) return -1;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return -1;
    rc = chanserv_db_logging_get(&db, channel_name, registered, enabled);
    chanserv_db_close(&db);
    return rc;
}

static int db_set_enabled(Server *server, const char *channel_name, int enabled) {
    ChanServDb db = {0};
    int rc;
    if (server == NULL || channel_name == NULL) return -1;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return -1;
    rc = chanserv_db_logging_set(&db, channel_name, enabled);
    chanserv_db_close(&db);
    return rc;
}

static int state_enabled(Server *server, const char *channel_name) {
    ChannelLogState *state = state_for(channel_name, 1);
    int registered = 0;
    int enabled = 0;
    if (state == NULL) return 0;
    if (!state->known) {
        if (db_get_enabled(server, channel_name, &registered, &enabled) != 0)
            return 0;
        state->known = 1;
        state->enabled = registered && enabled;
    }
    return state->enabled;
}

static void safe_channel_component(const char *channel_name,
                                   char *out, size_t out_size) {
    size_t used = 0U;
    const unsigned char *p;
    if (out == NULL || out_size == 0U) return;
    out[0] = '\0';
    if (channel_name == NULL) return;
    p = (const unsigned char *)channel_name;
    if (*p == '#' || *p == '&') ++p;
    while (*p != '\0' && used + 1U < out_size) {
        unsigned char ch = *p++;
        if (isalnum(ch) || ch == '.' || ch == '-' || ch == '_' ||
            ch == '[' || ch == ']' || ch == '{' || ch == '}' ||
            ch == '^' || ch == '~')
            out[used++] = (char)ch;
        else
            out[used++] = '_';
    }
    if (used == 0U && out_size > 1U) out[used++] = '_';
    out[used] = '\0';
}

static void log_path(const char *channel_name, const char *suffix,
                     char *path, size_t path_size) {
    char component[IRC_CHANNEL_NAME_MAX + 1U];
    safe_channel_component(channel_name, component, sizeof(component));
    (void)snprintf(path, path_size, "logs/%s.log.%s", component, suffix);
}

static int make_logs_dir(void) {
    return mkdir("logs", 0750) == 0 || errno == EEXIST ? 0 : -1;
}

static void format_suffix(const struct tm *local, char *out, size_t out_size) {
    (void)strftime(out, out_size, "%d%b%Y", local);
}

static void append_boundary(const char *channel_name, const char *suffix,
                            const struct tm *local, int midnight) {
    char path[IRCD_PATH_MAX + IRC_CHANNEL_NAME_MAX + 64U];
    char date_text[96];
    FILE *file;
    if (make_logs_dir() != 0) return;
    log_path(channel_name, suffix, path, sizeof(path));
    file = fopen(path, "a");
    if (file == NULL) return;
    if (midnight) {
        (void)strftime(date_text, sizeof(date_text), "%B %d %Y", local);
        (void)fprintf(file, "[00:00:00] --- %s 00:00:00.\n", date_text);
    } else {
        char clock_text[16];
        (void)strftime(clock_text, sizeof(clock_text), "%H:%M:%S", local);
        (void)strftime(date_text, sizeof(date_text), "%B %d %Y %H:%M:%S", local);
        (void)fprintf(file, "[%s] --- %s.\n", clock_text, date_text);
    }
    fclose(file);
}

static void ensure_file_for_time(ChannelLogState *state, time_t when) {
    struct tm local;
    char suffix[32];
    char path[IRCD_PATH_MAX + IRC_CHANNEL_NAME_MAX + 64U];
    struct stat st;
    if (state == NULL || localtime_r(&when, &local) == NULL) return;
    if (make_logs_dir() != 0) return;
    format_suffix(&local, suffix, sizeof(suffix));
    if (state->date_suffix[0] != '\0' && strcmp(state->date_suffix, suffix) != 0)
        append_boundary(state->channel, state->date_suffix, &local, 1);
    if (strcmp(state->date_suffix, suffix) != 0)
        (void)snprintf(state->date_suffix, sizeof(state->date_suffix), "%s", suffix);
    log_path(state->channel, suffix, path, sizeof(path));
    if (stat(path, &st) != 0 || st.st_size == 0)
        append_boundary(state->channel, suffix, &local, 0);
}

static int queue_event(Server *server, Channel *channel, time_t when,
                       const char *body) {
    ChanServDb db = {0};
    int rc;
    if (server == NULL || channel == NULL || body == NULL ||
        !state_enabled(server, channel->name)) return 0;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return -1;
    rc = chanserv_db_logging_queue_add(&db, channel->name, (long long)when, body);
    chanserv_db_close(&db);
    return rc;
}

static int flush_channel(Server *server, const char *channel_name) {
    ChanServDb db = {0};
    ChanServLogQueueRecord records[128];
    ChannelLogState *state;
    size_t count = 0U;
    int rc = -1;
    if (server == NULL || channel_name == NULL) return -1;
    state = state_for(channel_name, 1);
    if (state == NULL) return -1;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return -1;

    for (;;) {
        size_t i;
        long long last_id = 0;
        if (chanserv_db_logging_queue_fetch(&db, channel_name, records,
                                             sizeof(records) / sizeof(records[0]),
                                             &count) != 0)
            goto done;
        if (count == 0U) break;
        for (i = 0U; i < count; ++i) {
            time_t when = (time_t)records[i].event_time;
            struct tm local;
            char stamp[16];
            char path[IRCD_PATH_MAX + IRC_CHANNEL_NAME_MAX + 64U];
            FILE *file;
            if (localtime_r(&when, &local) == NULL) goto done;
            ensure_file_for_time(state, when);
            format_suffix(&local, state->date_suffix, sizeof(state->date_suffix));
            log_path(channel_name, state->date_suffix, path, sizeof(path));
            file = fopen(path, "a");
            if (file == NULL) goto done;
            (void)strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
            if (fprintf(file, "[%s] %s\n", stamp, records[i].body) < 0) {
                fclose(file);
                goto done;
            }
            if (fclose(file) != 0) goto done;
            last_id = records[i].id;
        }
        if (last_id != 0 &&
            chanserv_db_logging_queue_delete_through(&db, channel_name, last_id) != 0)
            goto done;
    }
    rc = 0;
done:
    chanserv_db_close(&db);
    return rc;
}

static int queue_channels(Server *server, char *buffer, size_t size) {
    ChanServDb db = {0};
    int rc;
    if (server == NULL || buffer == NULL || size == 0U) return -1;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return -1;
    rc = chanserv_db_logging_queue_list_channels(&db, buffer, size);
    chanserv_db_close(&db);
    return rc;
}

static int channel_oldest(Server *server, const char *channel_name,
                          long long *event_time) {
    ChanServDb db = {0};
    ChanServLogQueueRecord record;
    size_t count = 0U;
    int rc;
    if (event_time != NULL) *event_time = 0;
    if (server == NULL || channel_name == NULL || event_time == NULL) return -1;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return -1;
    rc = chanserv_db_logging_queue_fetch(&db, channel_name, &record, 1U, &count);
    if (rc == 0 && count == 1U) *event_time = record.event_time;
    chanserv_db_close(&db);
    return rc;
}

static void for_each_queued_channel(Server *server, int due_only, time_t now) {
    char channels[8192];
    char *save = NULL;
    char *name;
    if (queue_channels(server, channels, sizeof(channels)) != 0 || channels[0] == '\0') return;
    name = strtok_r(channels, ",", &save);
    while (name != NULL) {
        if (!due_only) {
            (void)flush_channel(server, name);
        } else {
            long long oldest = 0;
            if (channel_oldest(server, name, &oldest) == 0 && oldest != 0 &&
                oldest + IRCD_CHANNEL_LOG_BATCH_SECONDS <= (long long)now)
                (void)flush_channel(server, name);
        }
        name = strtok_r(NULL, ",", &save);
    }
}

void channel_log_join(Server *server, Channel *channel, Client *client) {
    char body[IRC_MESSAGE_BUFFER_SIZE + 256U];
    if (client == NULL || channel == NULL) return;
    (void)snprintf(body, sizeof(body), "%s (%s@%s) joined %s.",
                   client->nick, client->user, client->display_host, channel->name);
    (void)queue_event(server, channel, time(NULL), body);
}

void channel_log_part(Server *server, Channel *channel, Client *client,
                      const char *reason) {
    char body[IRC_MESSAGE_BUFFER_SIZE + 256U];
    if (client == NULL || channel == NULL) return;
    (void)snprintf(body, sizeof(body), "%s (%s@%s) left %s: %s",
                   client->nick, client->user, client->display_host, channel->name,
                   reason != NULL && *reason != '\0' ? reason : "Leaving");
    (void)queue_event(server, channel, time(NULL), body);
}

void channel_log_quit(Server *server, Channel *channel, Client *client,
                      const char *reason) {
    char body[IRC_MESSAGE_BUFFER_SIZE + 256U];
    if (client == NULL || channel == NULL) return;
    (void)snprintf(body, sizeof(body), "%s (%s@%s) left irc: Quit:  %s",
                   client->nick, client->user, client->display_host,
                   reason != NULL && *reason != '\0' ? reason : "Client Quit");
    (void)queue_event(server, channel, time(NULL), body);
}

void channel_log_message(Server *server, Channel *channel, Client *client,
                         const char *text, int is_notice) {
    char body[IRC_MESSAGE_BUFFER_SIZE + 256U];
    if (client == NULL || channel == NULL || text == NULL) return;
    if (is_notice)
        (void)snprintf(body, sizeof(body), "-%s- %s", client->nick, text);
    else
        (void)snprintf(body, sizeof(body), "<%s> %s", client->nick, text);
    (void)queue_event(server, channel, time(NULL), body);
}

void channel_log_flush_due(Server *server, time_t now) {
    for_each_queued_channel(server, 1, now);
}

void channel_log_flush_all(Server *server) {
    for_each_queued_channel(server, 0, time(NULL));
}

void channel_log_rotate_all(Server *server, time_t now) {
    ChannelLogState *state;
    struct tm local;
    char suffix[32];
    channel_log_flush_due(server, now);
    if (localtime_r(&now, &local) == NULL) return;
    format_suffix(&local, suffix, sizeof(suffix));
    for (state = states; state != NULL; state = state->next) {
        if (!state->known || !state->enabled || state->date_suffix[0] == '\0' ||
            strcmp(state->date_suffix, suffix) == 0)
            continue;
        (void)flush_channel(server, state->channel);
        ensure_file_for_time(state, now);
    }
}

static void chanserv_notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":ChanServ!service@%s NOTICE %s :%s",
                 server->config.server_name, client->nick, text);
}

int channel_log_handle_chanserv(Server *server, Client *client,
                                const char *text) {
    char copy[IRC_MESSAGE_BUFFER_SIZE];
    char *command;
    char *channel_name;
    char *field;
    char *value;
    int enable;
    int registered = 0;
    int current = 0;
    ChannelLogState *state;
    time_t now;
    struct tm local;

    if (server == NULL || client == NULL || text == NULL) return 0;
    (void)snprintf(copy, sizeof(copy), "%s", text);
    command = strtok(copy, " ");
    channel_name = command != NULL ? strtok(NULL, " ") : NULL;
    field = channel_name != NULL ? strtok(NULL, " ") : NULL;
    value = field != NULL ? strtok(NULL, " ") : NULL;
    if (command == NULL || channel_name == NULL || field == NULL ||
        strcasecmp(command, "SET") != 0 || strcasecmp(field, "LOGGING") != 0)
        return 0;

    if (value == NULL ||
        (strcasecmp(value, "ON") != 0 && strcasecmp(value, "OFF") != 0)) {
        chanserv_notice(server, client, "Syntax: SET <#channel> LOGGING ON|OFF");
        return 1;
    }
    if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) {
        chanserv_notice(server, client,
                        "Only IRC operators and network administrators may change channel logging.");
        return 1;
    }
    if (db_get_enabled(server, channel_name, &registered, &current) != 0 || !registered) {
        chanserv_notice(server, client, "Channel is not registered.");
        return 1;
    }

    enable = strcasecmp(value, "ON") == 0;
    state = state_for(channel_name, 1);
    if (!enable) (void)flush_channel(server, channel_name);
    if (db_set_enabled(server, channel_name, enable) != 0) {
        chanserv_notice(server, client, "Unable to update channel logging.");
        return 1;
    }

    if (state != NULL) {
        state->known = 1;
        if (!enable && state->enabled && state->date_suffix[0] != '\0' &&
            localtime_r(&(time_t){time(NULL)}, &local) != NULL)
            append_boundary(state->channel, state->date_suffix, &local, 0);
        state->enabled = enable;
        if (enable) {
            now = time(NULL);
            ensure_file_for_time(state, now);
        }
    }

    chanserv_notice(server, client,
                    enable ? "Channel logging enabled." : "Channel logging disabled.");
    return 1;
}
