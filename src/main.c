#include "ban_db.h"
#include "channel_log.h"
#include "chanserv_db.h"
#include "chanserv_persist.h"
#include "commands.h"
#include "config.h"
#include "geoban_db.h"
#include "history_db.h"
#include "memoserv.h"
#include "memoserv_db.h"
#include "nickserv.h"
#include "nickserv_db.h"
#include "operator_db.h"
#include "presence.h"
#include "runtime_config.h"
#include "server.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

static Server server;
static volatile sig_atomic_t server_active;
static volatile sig_atomic_t shutdown_signal_pending;

static void handle_shutdown_signal(int signo) {
    (void)signo;
    shutdown_signal_pending = 1;
    if (server_active) {
        server.shutdown_requested = 1;
        server.restart_requested = 1;
    }
}

static int install_shutdown_handlers(void) {
    struct sigaction action;
    action.sa_handler = handle_shutdown_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGTERM, &action, NULL) != 0) return -1;
    if (sigaction(SIGINT, &action, NULL) != 0) return -1;
    return 0;
}

static int ensure_databases(const ServerConfig *config) {
    OperatorDb operators;
    BanDb bans;
    NickServDb nickserv;
    ChanServDb chanserv;
    MemoServDb memoserv;

    if (operator_db_open(&operators, config->operators_db) != 0) {
        fprintf(stderr, "Failed to open operator database: %s\n", config->operators_db);
        return -1;
    }
    operator_db_close(&operators);

    if (ban_db_open(&bans, config->bans_db) != 0) {
        fprintf(stderr, "Failed to open ban database: %s\n", config->bans_db);
        return -1;
    }
    ban_db_close(&bans);

    if (nickserv_db_open(&nickserv, config->nickserv_db) != 0) {
        fprintf(stderr, "Failed to open NickServ database: %s\n", config->nickserv_db);
        return -1;
    }
    nickserv_db_close(&nickserv);

    if (chanserv_db_open(&chanserv, config->chanserv_db) != 0) {
        fprintf(stderr, "Failed to open ChanServ database: %s\n", config->chanserv_db);
        return -1;
    }
    chanserv_db_close(&chanserv);
    if (chanserv_persist_init(config->chanserv_db) != 0) {
        fprintf(stderr, "Failed to initialize ChanServ persistence: %s\n",
                config->chanserv_db);
        return -1;
    }

    if (memoserv_db_open(&memoserv, config->memoserv_db) != 0) {
        fprintf(stderr, "Failed to open MemoServ database: %s\n", config->memoserv_db);
        return -1;
    }
    memoserv_db_close(&memoserv);
    return 0;
}

/** Program entry point. Usage: scratchircd [config-file] */
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : IRCD_DEFAULT_CONFIG_FILE;
    int exit_status = 0;

    if (install_shutdown_handlers() != 0) {
        perror("sigaction");
        return 1;
    }

    for (;;) {
        ServerConfig config;
        int restart;
        int runtime_failure;

        if (shutdown_signal_pending) break;

        runtime_config_defaults(&config);
        if (runtime_config_load(&config, path) != 0) {
            if (argc > 1 || errno != ENOENT) {
                fprintf(stderr, "Failed to load configuration: %s\n", path);
                return 1;
            }
            clearerr(stderr);
        }
        if (shutdown_signal_pending) break;

        if (ensure_databases(&config) != 0) return 1;
        if (shutdown_signal_pending) {
            chanserv_persist_reset();
            break;
        }

        if (server_init(&server, &config) != 0) {
            fprintf(stderr, "Failed to start %s on port %s\n",
                    config.server_name, config.port);
            return 1;
        }
        server.started_at = time(NULL);
        command_common_set_server_name(config.server_name);

        printf("%s (%s) listening on port %s with %zu listener(s)\n",
               config.server_name, IRCD_VERSION, config.port,
               server.listener_count);
        /* Mark the server active before consulting the process-level pending
         * signal. A SIGTERM/SIGINT arriving on either side of this transition
         * therefore cannot be lost between initialization and server_run(). */
        server_active = 1;
        if (shutdown_signal_pending) {
            server.shutdown_requested = 1;
            server.restart_requested = 1;
        }
        server_run(&server);
        server_active = 0;
        runtime_failure = !server.restart_requested && !server.shutdown_requested;
        restart = server.restart_requested && !server.shutdown_requested;
        if (runtime_failure)
            fprintf(stderr, "ScratchIRCd event loop terminated unexpectedly; shutting down\n");
        server_destroy(&server);

        /* Channel-log rows are already durable in SQLite. Make one bounded
         * best-effort pass after disconnect-generated QUIT records are queued;
         * any remaining backlog survives shutdown/restart and is recovered by
         * the normal oldest-first flusher instead of blocking here indefinitely. */
        channel_log_flush_due(&server, time(NULL) + IRCD_CHANNEL_LOG_BATCH_SECONDS);
        presence_reset_runtime_state();
        chanserv_persist_reset();
        history_db_reset_shared();
        command_common_reset_state();
        command_motd_reset_cache();
        command_rules_reset_cache();
        nickserv_reset_runtime_state();
        memoserv_reset_runtime_state();
        ban_db_reset_runtime_state();
        geoban_db_reset_runtime_state();

        if (runtime_failure) {
            exit_status = 1;
            break;
        }
        /* A process signal received after server_run() returned but before the
         * in-process RESTART loop reaches this point must override RESTART. */
        if (shutdown_signal_pending || !restart) break;
        fprintf(stdout, "Restarting ScratchIRCd using %s\n", path);
    }

    return exit_status;
}
