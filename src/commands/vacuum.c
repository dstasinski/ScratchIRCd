/** @file vacuum.c @brief Network-administrator-only SQLite maintenance. */
#include "commands.h"
#include "numerics.h"
#include "oper.h"

#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define VACUUM_COUNT 6U
static const char *const vacuum_names[VACUUM_COUNT] = {
    "NICKSERV", "CHANSERV", "MEMOSERV", "OPERATORS", "BANS", "HISTORY"
};

typedef struct VacuumJob {
    pthread_t thread;
    pthread_mutex_t mutex;
    int running;
    int joinable;
    unsigned int count;
    unsigned int completed;
    unsigned int failed;
    unsigned int reported;
    int finish_reported;
    char results[VACUUM_COUNT][180];
    char current[24];
    char error[160];
    char paths[VACUUM_COUNT][IRCD_CONFIG_PATH_MAX + 1U];
    unsigned int targets[VACUUM_COUNT];
} VacuumJob;

static VacuumJob job = {.mutex = PTHREAD_MUTEX_INITIALIZER};

static void notice(Server *server, Client *client, const char *message) {
    client_sendf(client, ":%s NOTICE %s :VACUUM %s",
                 server->config.server_name, client->nick, message);
}

static void *run_vacuum(void *unused) {
    unsigned int n;
    (void)unused;
    for (n = 0U; n < job.count; ++n) {
        unsigned int index = job.targets[n];
        sqlite3 *db = NULL;
        int rc;
        char error[160] = "";
        pthread_mutex_lock(&job.mutex);
        (void)snprintf(job.current, sizeof(job.current), "%s", vacuum_names[index]);
        pthread_mutex_unlock(&job.mutex);

        rc = sqlite3_open_v2(job.paths[index], &db, SQLITE_OPEN_READWRITE, NULL);
        if (rc == SQLITE_OK) {
            (void)sqlite3_busy_timeout(db, 1000);
            rc = sqlite3_exec(db, "VACUUM;", NULL, NULL, NULL);
        }
        if (rc != SQLITE_OK)
            (void)snprintf(error, sizeof(error), "%s: %.120s",
                           vacuum_names[index],
                           db != NULL ? sqlite3_errmsg(db) : "open failed");
        if (db != NULL) (void)sqlite3_close(db);

        pthread_mutex_lock(&job.mutex);
        if (rc == SQLITE_OK) ++job.completed;
        else {
            ++job.failed;
            (void)snprintf(job.error, sizeof(job.error), "%s", error);
        }
        (void)snprintf(job.results[n], sizeof(job.results[n]), "%s %s",
                       vacuum_names[index], rc == SQLITE_OK ? "completed" : error);
        pthread_mutex_unlock(&job.mutex);
    }
    pthread_mutex_lock(&job.mutex);
    job.current[0] = '\0';
    job.running = 0;
    pthread_mutex_unlock(&job.mutex);
    return NULL;
}

/* Only the main IRC thread emits notices; worker only writes protected state. */
void command_vacuum_poll(Server *server) {
    char results[VACUUM_COUNT][180];
    unsigned int count = 0U, completed, failed, i;
    int finished = 0;
    pthread_mutex_lock(&job.mutex);
    while (job.reported < job.completed + job.failed && count < VACUUM_COUNT) {
        (void)snprintf(results[count], sizeof(results[count]), "%s",
                       job.results[job.reported]);
        ++job.reported;
        ++count;
    }
    completed = job.completed;
    failed = job.failed;
    if (!job.running && job.joinable && !job.finish_reported) {
        job.finish_reported = 1;
        finished = 1;
    }
    pthread_mutex_unlock(&job.mutex);
    for (i = 0U; i < count; ++i)
        snotice_broadcast(server, SNOTICE_ADMIN, "VACUUM %s", results[i]);
    if (finished)
        snotice_broadcast(server, SNOTICE_ADMIN,
                          "VACUUM finished: %u succeeded, %u failed",
                          completed, failed);
}

void command_vacuum_shutdown(void) {
    pthread_mutex_lock(&job.mutex);
    if (job.joinable) {
        pthread_mutex_unlock(&job.mutex);
        (void)pthread_join(job.thread, NULL);
        pthread_mutex_lock(&job.mutex);
        job.joinable = 0;
    }
    pthread_mutex_unlock(&job.mutex);
}

CommandResult command_vacuum(Server *server, Client *client, char *params) {
    const char *paths[VACUUM_COUNT];
    char option[32];
    char status[320];
    const char *cursor = params != NULL ? params : "";
    const char *end;
    unsigned int i;
    unsigned int selected = VACUUM_COUNT;
    int all;

    if (command_require_registered(client)) return COMMAND_KEEP_CLIENT;
    if (!oper_permission_has(client->oper_permissions, OPER_PERMISSION_NETADMIN)) {
        client_sendf(client, ERR_NOPRIVILEGES,
                     server->config.server_name, client->nick);
        return COMMAND_KEEP_CLIENT;
    }

    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor == ':') ++cursor;
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    end = cursor;
    while (*end != '\0' && *end != ' ' && *end != '\t') ++end;
    if ((size_t)(end - cursor) >= sizeof(option)) {
        notice(server, client, "Syntax: LIST|STATUS|ALL|database");
        return COMMAND_KEEP_CLIENT;
    }
    memcpy(option, cursor, (size_t)(end - cursor));
    option[end - cursor] = '\0';
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0') {
        notice(server, client, "Syntax: LIST|STATUS|ALL|database");
        return COMMAND_KEEP_CLIENT;
    }

    paths[0] = server->config.nickserv_db;
    paths[1] = server->config.chanserv_db;
    paths[2] = server->config.memoserv_db;
    paths[3] = server->config.operators_db;
    paths[4] = server->config.bans_db;
    paths[5] = server->config.history_db;

    if (strcasecmp(option, "LIST") == 0) {
        for (i = 0U; i < VACUUM_COUNT; ++i) {
            client_sendf(client, ":%s NOTICE %s :VACUUM %s: %s",
                         server->config.server_name, client->nick,
                         vacuum_names[i], paths[i]);
            if (client->output_overflowed) break;
        }
        return COMMAND_KEEP_CLIENT;
    }
    if (strcasecmp(option, "STATUS") == 0) {
        pthread_mutex_lock(&job.mutex);
        (void)snprintf(status, sizeof(status),
                       "%s current=%s completed=%u failed=%u last_error=%s",
                       job.running ? "running" : "idle",
                       job.current[0] ? job.current : "-",
                       job.completed, job.failed,
                       job.error[0] ? job.error : "-");
        pthread_mutex_unlock(&job.mutex);
        notice(server, client, status);
        return COMMAND_KEEP_CLIENT;
    }
    all = strcasecmp(option, "ALL") == 0;
    for (i = 0U; i < VACUUM_COUNT; ++i)
        if (strcasecmp(option, vacuum_names[i]) == 0) selected = i;
    if (!all && selected == VACUUM_COUNT) {
        notice(server, client, "Syntax: LIST|STATUS|ALL|NICKSERV|CHANSERV|MEMOSERV|OPERATORS|BANS|HISTORY");
        return COMMAND_KEEP_CLIENT;
    }

    pthread_mutex_lock(&job.mutex);
    if (job.running) {
        pthread_mutex_unlock(&job.mutex);
        notice(server, client, "already running");
        return COMMAND_KEEP_CLIENT;
    }
    if (job.joinable) {
        pthread_mutex_unlock(&job.mutex);
        command_vacuum_shutdown();
        pthread_mutex_lock(&job.mutex);
    }
    job.count = all ? VACUUM_COUNT : 1U;
    job.completed = 0U;
    job.failed = 0U;
    job.reported = 0U;
    job.finish_reported = 0;
    job.current[0] = '\0';
    job.error[0] = '\0';
    for (i = 0U; i < VACUUM_COUNT; ++i)
        (void)snprintf(job.paths[i], sizeof(job.paths[i]), "%s", paths[i]);
    for (i = 0U; i < job.count; ++i)
        job.targets[i] = all ? i : selected;
    job.running = 1;
    if (pthread_create(&job.thread, NULL, run_vacuum, NULL) != 0) {
        job.running = 0;
        pthread_mutex_unlock(&job.mutex);
        notice(server, client, "unable to start worker");
        return COMMAND_KEEP_CLIENT;
    }
    job.joinable = 1;
    pthread_mutex_unlock(&job.mutex);
    snotice_broadcast(server, SNOTICE_ADMIN, "VACUUM %s started by %s",
                      option, client->nick);
    notice(server, client, "started; use /VACUUM STATUS for progress");
    return COMMAND_KEEP_CLIENT;
}
