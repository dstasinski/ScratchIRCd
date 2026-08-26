/** @file channel_log.c @brief Durable five-minute batched channel logging. */
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
static Server *active_server;
static ChanServDb log_db;
static char log_db_path[IRCD_PATH_MAX + 1U];

static ChanServDb *shared_db(Server *server) {
    if (server == NULL || server->config.chanserv_db[0] == '\0') return NULL;
    if (log_db.db != NULL && strcmp(log_db_path, server->config.chanserv_db) == 0)
        return &log_db;
    if (log_db.db != NULL) {
        chanserv_db_close(&log_db);
        log_db_path[0] = '\0';
    }
    if (chanserv_db_open(&log_db, server->config.chanserv_db) != 0) return NULL;
    (void)snprintf(log_db_path, sizeof(log_db_path), "%s", server->config.chanserv_db);
    return &log_db;
}

int channel_log_init(Server *server) {
    ChanServDb *db;
    if (server == NULL) return -1;
    active_server = server;
    db = shared_db(server);
    if (db == NULL) return -1;
    return chanserv_db_logging_ensure_schema(db);
}

static ChannelLogState *state_for(const char *name, int create) {
    ChannelLogState *state;
    for (state = states; state != NULL; state = state->next)
        if (strcasecmp(state->channel, name) == 0) return state;
    if (!create) return NULL;
    state = calloc(1U, sizeof(*state));
    if (state == NULL) return NULL;
    (void)snprintf(state->channel, sizeof(state->channel), "%s", name);
    state->next = states;
    states = state;
    return state;
}

static int db_get_enabled(Server *server, const char *name,
                          int *registered, int *enabled) {
    ChanServDb *db;
    if (registered) *registered = 0;
    if (enabled) *enabled = 0;
    if (!server || !name || (db = shared_db(server)) == NULL) return -1;
    return chanserv_db_logging_get(db, name, registered, enabled);
}

static int db_set_enabled(Server *server, const char *name, int enabled) {
    ChanServDb *db;
    if (!server || !name || (db = shared_db(server)) == NULL) return -1;
    return chanserv_db_logging_set(db, name, enabled);
}

static int state_enabled(Server *server, const char *name) {
    ChannelLogState *state;
    int registered = 0, enabled = 0;
    if (!server || !name) return 0;
    active_server = server;
    state = state_for(name, 1);
    if (!state) return 0;
    if (!state->known) {
        if (db_get_enabled(server, name, &registered, &enabled) != 0) return 0;
        state->known = 1;
        state->enabled = registered && enabled;
    }
    return state->enabled;
}

static void safe_component(const char *name, char *out, size_t size) {
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)name;
    if (!out || size == 0) return;
    out[0] = '\0';
    if (!p) return;
    if (*p == '#' || *p == '&') ++p;
    while (*p && used + 1 < size) {
        unsigned char ch = *p++;
        if (isalnum(ch) || ch == '.' || ch == '-' || ch == '_' || ch == '[' ||
            ch == ']' || ch == '{' || ch == '}' || ch == '^' || ch == '~') out[used++] = (char)ch;
        else out[used++] = '_';
    }
    if (used == 0 && size > 1) out[used++] = '_';
    out[used] = '\0';
}

static void log_path(const char *name, const char *suffix, char *path, size_t size) {
    char component[IRC_CHANNEL_NAME_MAX + 1U];
    safe_component(name, component, sizeof(component));
    (void)snprintf(path, size, "logs/%s.log.%s", component, suffix);
}

static int make_logs_dir(void) { return mkdir("logs", 0750) == 0 || errno == EEXIST ? 0 : -1; }
static void suffix_for(const struct tm *tm, char *out, size_t size) { (void)strftime(out, size, "%d%b%Y", tm); }

static void boundary(const char *channel, const char *suffix, const struct tm *local, int midnight) {
    char path[IRCD_PATH_MAX + IRC_CHANNEL_NAME_MAX + 64U], text[96], stamp[16];
    FILE *file;
    if (make_logs_dir() != 0) return;
    log_path(channel, suffix, path, sizeof(path));
    file = fopen(path, "a");
    if (!file) return;
    if (midnight) {
        (void)strftime(text, sizeof(text), "%B %d %Y", local);
        (void)fprintf(file, "[00:00:00] --- %s 00:00:00.\n", text);
    } else {
        (void)strftime(stamp, sizeof(stamp), "%H:%M:%S", local);
        (void)strftime(text, sizeof(text), "%B %d %Y %H:%M:%S", local);
        (void)fprintf(file, "[%s] --- %s.\n", stamp, text);
    }
    (void)fclose(file);
}

static void ensure_file(ChannelLogState *state, time_t when) {
    struct tm local;
    char suffix[32], path[IRCD_PATH_MAX + IRC_CHANNEL_NAME_MAX + 64U];
    struct stat st;
    if (!state || localtime_r(&when, &local) == NULL || make_logs_dir() != 0) return;
    suffix_for(&local, suffix, sizeof(suffix));
    if (state->date_suffix[0] && strcmp(state->date_suffix, suffix) != 0)
        boundary(state->channel, state->date_suffix, &local, 1);
    if (strcmp(state->date_suffix, suffix) != 0)
        (void)snprintf(state->date_suffix, sizeof(state->date_suffix), "%s", suffix);
    log_path(state->channel, suffix, path, sizeof(path));
    if (stat(path, &st) != 0 || st.st_size == 0) boundary(state->channel, suffix, &local, 0);
}

static int enqueue(Server *server, Channel *channel, time_t when, const char *body) {
    ChanServDb *db;
    if (!server || !channel || !body || !state_enabled(server, channel->name)) return 0;
    db = shared_db(server);
    if (db == NULL) return -1;
    return chanserv_db_logging_queue_add(db, channel->name, (long long)when, body);
}

static int flush_channel(Server *server, const char *name) {
    ChanServDb *db;
    ChanServLogQueueRecord rows[128];
    ChannelLogState *state;
    size_t count;
    if (!server || !name) return -1;
    active_server = server;
    state = state_for(name, 1);
    db = shared_db(server);
    if (!state || db == NULL) return -1;
    for (;;) {
        size_t i;
        long long last_id = 0;
        if (chanserv_db_logging_queue_fetch(db, name, rows, 128, &count) != 0) return -1;
        if (!count) break;
        for (i = 0; i < count; ++i) {
            time_t when = (time_t)rows[i].event_time;
            struct tm local;
            char suffix[32], stamp[16], path[IRCD_PATH_MAX + IRC_CHANNEL_NAME_MAX + 64U];
            FILE *file;
            if (localtime_r(&when, &local) == NULL) return -1;
            ensure_file(state, when);
            suffix_for(&local, suffix, sizeof(suffix));
            log_path(name, suffix, path, sizeof(path));
            file = fopen(path, "a");
            if (!file) return -1;
            (void)strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
            if (fprintf(file, "[%s] %s\n", stamp, rows[i].body) < 0 || fclose(file) != 0)
                return -1;
            last_id = rows[i].id;
        }
        if (chanserv_db_logging_queue_delete_through(db, name, last_id) != 0) return -1;
    }
    return 0;
}

static int list_queued(Server *server, char *buffer, size_t size) {
    ChanServDb *db;
    if (!server || !buffer || !size || (db = shared_db(server)) == NULL) return -1;
    return chanserv_db_logging_queue_list_channels(db, buffer, size);
}

static long long oldest_for(Server *server, const char *name) {
    ChanServDb *db;
    ChanServLogQueueRecord row;
    size_t count = 0;
    long long when = 0;
    if (!server || !name || (db = shared_db(server)) == NULL) return 0;
    if (chanserv_db_logging_queue_fetch(db, name, &row, 1, &count) == 0 && count == 1)
        when = row.event_time;
    return when;
}

static void flush_queued(Server *server, int due_only, time_t now) {
    char channels[8192], *save = NULL, *name;
    if (!server || list_queued(server, channels, sizeof(channels)) != 0 || !channels[0]) return;
    name = strtok_r(channels, ",", &save);
    while (name) {
        long long oldest = oldest_for(server, name);
        if (!due_only || (oldest && oldest + IRCD_CHANNEL_LOG_BATCH_SECONDS <= (long long)now))
            (void)flush_channel(server, name);
        name = strtok_r(NULL, ",", &save);
    }
}

void channel_log_join(Server *server, Channel *channel, Client *client) {
    char body[IRCD_MESSAGE_BUFFER_SIZE + 256U];
    if (!channel || !client) return;
    (void)snprintf(body, sizeof(body), "%s (%s@%s) joined %s.", client->nick, client->user, client->display_host, channel->name);
    (void)enqueue(server, channel, time(NULL), body);
}
void channel_log_part(Server *server, Channel *channel, Client *client, const char *reason) {
    char body[IRCD_MESSAGE_BUFFER_SIZE + 256U];
    if (!channel || !client) return;
    (void)snprintf(body, sizeof(body), "%s (%s@%s) left %s: %s", client->nick, client->user, client->display_host, channel->name, reason && *reason ? reason : "Leaving");
    (void)enqueue(server, channel, time(NULL), body);
}
void channel_log_quit(Server *server, Channel *channel, Client *client, const char *reason) {
    char body[IRCD_MESSAGE_BUFFER_SIZE + 256U];
    if (!channel || !client) return;
    (void)snprintf(body, sizeof(body), "%s (%s@%s) left irc: Quit:  %s", client->nick, client->user, client->display_host, reason && *reason ? reason : "Client Quit");
    (void)enqueue(server, channel, time(NULL), body);
}
void channel_log_message(Server *server, Channel *channel, Client *client, const char *text, int notice) {
    char body[IRCD_MESSAGE_BUFFER_SIZE + 256U];
    if (!channel || !client || !text) return;
    if (notice) (void)snprintf(body, sizeof(body), "-%s- %s", client->nick, text);
    else (void)snprintf(body, sizeof(body), "<%s> %s", client->nick, text);
    (void)enqueue(server, channel, time(NULL), body);
}

void channel_log_flush_due(Server *server, time_t now) { if (server) { active_server = server; flush_queued(server, 1, now); } }
void channel_log_flush_all(Server *server) { if (server) { active_server = server; flush_queued(server, 0, time(NULL)); } }

void channel_log_rotate_all(time_t now) {
    ChannelLogState *state;
    struct tm local;
    char suffix[32];
    if (!active_server) return;
    channel_log_flush_due(active_server, now);
    if (localtime_r(&now, &local) == NULL) return;
    suffix_for(&local, suffix, sizeof(suffix));
    for (state = states; state; state = state->next) {
        if (!state->known || !state->enabled || !state->date_suffix[0] || strcmp(state->date_suffix, suffix) == 0) continue;
        (void)flush_channel(active_server, state->channel);
        ensure_file(state, now);
    }
}

static void cs_notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":ChanServ!service@%s NOTICE %s :%s", server->config.server_name, client->nick, text);
}

int channel_log_handle_chanserv(Server *server, Client *client, const char *text) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE], *command, *channel, *field, *value;
    int enable, registered = 0, current = 0;
    ChannelLogState *state;
    struct tm local;
    time_t now;
    if (!server || !client || !text) return 0;
    active_server = server;
    (void)snprintf(copy, sizeof(copy), "%s", text);
    command = strtok(copy, " "); channel = command ? strtok(NULL, " ") : NULL;
    field = channel ? strtok(NULL, " ") : NULL; value = field ? strtok(NULL, " ") : NULL;
    if (!command || !channel || !field || strcasecmp(command, "SET") || strcasecmp(field, "LOGGING")) return 0;
    if (!value || (strcasecmp(value, "ON") && strcasecmp(value, "OFF"))) { cs_notice(server, client, "Syntax: SET <#channel> LOGGING ON|OFF"); return 1; }
    if (!client_mode_has(client->modes, CLIENT_MODE_OPER | CLIENT_MODE_NETADMIN)) { cs_notice(server, client, "Only IRC operators and network administrators may change channel logging."); return 1; }
    if (db_get_enabled(server, channel, &registered, &current) != 0 || !registered) { cs_notice(server, client, "Channel is not registered."); return 1; }
    enable = strcasecmp(value, "ON") == 0;
    state = state_for(channel, 1);
    if (!enable) (void)flush_channel(server, channel);
    if (db_set_enabled(server, channel, enable) != 0) { cs_notice(server, client, "Unable to update channel logging."); return 1; }
    if (state) {
        state->known = 1;
        if (!enable && state->enabled && state->date_suffix[0] && localtime_r(&(time_t){time(NULL)}, &local) != NULL)
            boundary(state->channel, state->date_suffix, &local, 0);
        state->enabled = enable;
        if (enable) { now = time(NULL); ensure_file(state, now); }
    }
    cs_notice(server, client, enable ? "Channel logging enabled." : "Channel logging disabled.");
    return 1;
}
