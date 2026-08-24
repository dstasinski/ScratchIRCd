/**
 * @file channel_log.c
 * @brief Optional ChanServ-controlled per-channel text logging.
 */

#include "channel_log.h"
#include "chanserv_db.h"
#include "modes.h"

#include <ctype.h>
#include <errno.h>
#include <sqlite3.h>
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

static int logging_column_exists(sqlite3 *db) {
    sqlite3_stmt *stmt = NULL;
    int found = 0;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(channels)", -1, &stmt, NULL) != SQLITE_OK)
        return 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name != NULL && strcmp(name, "logging_enabled") == 0) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

static int ensure_logging_column(sqlite3 *db) {
    char *error = NULL;
    int rc;
    if (logging_column_exists(db)) return 0;
    rc = sqlite3_exec(db,
        "ALTER TABLE channels ADD COLUMN logging_enabled INTEGER NOT NULL DEFAULT 0",
        NULL, NULL, &error);
    if (rc != SQLITE_OK) {
        if (error != NULL) fprintf(stderr, "ChanServ logging schema: %s\n", error);
        sqlite3_free(error);
        return -1;
    }
    return 0;
}

static int db_get_enabled(Server *server, const char *channel_name,
                          int *registered, int *enabled) {
    ChanServDb db = {0};
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (registered != NULL) *registered = 0;
    if (enabled != NULL) *enabled = 0;
    if (server == NULL || channel_name == NULL) return -1;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return -1;
    if (ensure_logging_column(db.db) != 0 ||
        sqlite3_prepare_v2(db.db,
            "SELECT enabled,logging_enabled FROM channels WHERE name=?1",
            -1, &stmt, NULL) != SQLITE_OK) {
        chanserv_db_close(&db);
        return -1;
    }
    sqlite3_bind_text(stmt, 1, channel_name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int channel_enabled = sqlite3_column_int(stmt, 0) != 0;
        if (registered != NULL) *registered = channel_enabled;
        if (enabled != NULL)
            *enabled = channel_enabled && sqlite3_column_int(stmt, 1) != 0;
        sqlite3_finalize(stmt);
        chanserv_db_close(&db);
        return 0;
    }
    sqlite3_finalize(stmt);
    chanserv_db_close(&db);
    return rc == SQLITE_DONE ? 0 : -1;
}

static int db_set_enabled(Server *server, const char *channel_name, int enabled) {
    ChanServDb db = {0};
    sqlite3_stmt *stmt = NULL;
    int rc;
    if (server == NULL || channel_name == NULL) return -1;
    if (chanserv_db_open(&db, server->config.chanserv_db) != 0) return -1;
    if (ensure_logging_column(db.db) != 0 ||
        sqlite3_prepare_v2(db.db,
            "UPDATE channels SET logging_enabled=?1,updated_at=unixepoch() "
            "WHERE name=?2 AND enabled=1",
            -1, &stmt, NULL) != SQLITE_OK) {
        chanserv_db_close(&db);
        return -1;
    }
    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 2, channel_name, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE || sqlite3_changes(db.db) == 0) {
        chanserv_db_close(&db);
        return -1;
    }
    chanserv_db_close(&db);
    return 0;
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
            ch == '^' || ch == '~') {
            out[used++] = (char)ch;
        } else {
            out[used++] = '_';
        }
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
    if (mkdir("logs", 0750) == 0 || errno == EEXIST) return 0;
    return -1;
}

static void format_suffix(const struct tm *local, char *out, size_t out_size) {
    (void)strftime(out, out_size, "%d%b%Y", local);
}

static void append_boundary(const char *channel_name, const char *suffix,
                            const struct tm *local, int midnight) {
    char path[IRCD_PATH_MAX + IRC_CHANNEL_NAME_MAX + 64U];
    char date_text[96];
    FILE *file;
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

static void ensure_current_file(ChannelLogState *state, time_t now) {
    struct tm local;
    char suffix[32];
    char path[IRCD_PATH_MAX + IRC_CHANNEL_NAME_MAX + 64U];
    struct stat st;
    if (state == NULL || !state->enabled || localtime_r(&now, &local) == NULL) return;
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

static FILE *open_log(Server *server, Channel *channel, time_t now,
                      struct tm *local) {
    ChannelLogState *state;
    char path[IRCD_PATH_MAX + IRC_CHANNEL_NAME_MAX + 64U];
    if (server == NULL || channel == NULL || local == NULL ||
        !state_enabled(server, channel->name)) return NULL;
    state = state_for(channel->name, 0);
    if (state == NULL || localtime_r(&now, local) == NULL) return NULL;
    ensure_current_file(state, now);
    log_path(channel->name, state->date_suffix, path, sizeof(path));
    return fopen(path, "a");
}

static void timestamp(const struct tm *local, char *out, size_t out_size) {
    (void)strftime(out, out_size, "%H:%M:%S", local);
}

void channel_log_join(Server *server, Channel *channel, Client *client) {
    time_t now = time(NULL);
    struct tm local;
    char stamp[16];
    FILE *file = open_log(server, channel, now, &local);
    if (file == NULL || client == NULL) { if (file != NULL) fclose(file); return; }
    timestamp(&local, stamp, sizeof(stamp));
    (void)fprintf(file, "[%s] %s (%s@%s) joined %s.\n",
                  stamp, client->nick, client->user, client->display_host, channel->name);
    fclose(file);
}

void channel_log_part(Server *server, Channel *channel, Client *client,
                      const char *reason) {
    time_t now = time(NULL);
    struct tm local;
    char stamp[16];
    FILE *file = open_log(server, channel, now, &local);
    if (file == NULL || client == NULL) { if (file != NULL) fclose(file); return; }
    timestamp(&local, stamp, sizeof(stamp));
    (void)fprintf(file, "[%s] %s (%s@%s) left %s: %s\n",
                  stamp, client->nick, client->user, client->display_host,
                  channel->name, reason != NULL && *reason != '\0' ? reason : "Leaving");
    fclose(file);
}

void channel_log_quit(Server *server, Channel *channel, Client *client,
                      const char *reason) {
    time_t now = time(NULL);
    struct tm local;
    char stamp[16];
    FILE *file = open_log(server, channel, now, &local);
    if (file == NULL || client == NULL) { if (file != NULL) fclose(file); return; }
    timestamp(&local, stamp, sizeof(stamp));
    (void)fprintf(file, "[%s] %s (%s@%s) left irc: Quit:  %s\n",
                  stamp, client->nick, client->user, client->display_host,
                  reason != NULL && *reason != '\0' ? reason : "Client Quit");
    fclose(file);
}

void channel_log_message(Server *server, Channel *channel, Client *client,
                         const char *text, int is_notice) {
    time_t now = time(NULL);
    struct tm local;
    char stamp[16];
    FILE *file = open_log(server, channel, now, &local);
    if (file == NULL || client == NULL || text == NULL) {
        if (file != NULL) fclose(file);
        return;
    }
    timestamp(&local, stamp, sizeof(stamp));
    if (is_notice)
        (void)fprintf(file, "[%s] -%s- %s\n", stamp, client->nick, text);
    else
        (void)fprintf(file, "[%s] <%s> %s\n", stamp, client->nick, text);
    fclose(file);
}

void channel_log_rotate_all(time_t now) {
    ChannelLogState *state;
    for (state = states; state != NULL; state = state->next)
        if (state->known && state->enabled) ensure_current_file(state, now);
}

static void chanserv_notice(Server *server, Client *client, const char *text) {
    client_sendf(client, ":ChanServ!service@%s NOTICE %s :%s",
                 server->config.server_name, client->nick, text);
}

int channel_log_handle_chanserv(Server *server, Client *client,
                                const char *text) {
    char copy[IRCD_MESSAGE_BUFFER_SIZE];
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
        chanserv_notice(server, client,
                        "Syntax: SET <#channel> LOGGING ON|OFF");
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
    if (db_set_enabled(server, channel_name, enable) != 0) {
        chanserv_notice(server, client, "Unable to update channel logging.");
        return 1;
    }

    state = state_for(channel_name, 1);
    if (state != NULL) {
        state->known = 1;
        if (!enable && state->enabled && state->date_suffix[0] != '\0' &&
            localtime_r(&(time_t){time(NULL)}, &local) != NULL) {
            append_boundary(state->channel, state->date_suffix, &local, 0);
        }
        state->enabled = enable;
        if (enable) {
            now = time(NULL);
            ensure_current_file(state, now);
        }
    }

    chanserv_notice(server, client,
                    enable ? "Channel logging enabled." : "Channel logging disabled.");
    return 1;
}
