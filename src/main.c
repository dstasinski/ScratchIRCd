#include "ban_db.h"
#include "channel_log.h"
#include "chanserv_db.h"
#include "config.h"
#include "geoip.h"
#include "memoserv_db.h"
#include "nickserv_db.h"
#include "operator_db.h"
#include "runtime_config.h"
#include "server.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>

static Server *active_server = NULL;

static void handle_shutdown_signal(int signo) {
    (void)signo;
    if (active_server != NULL) {
        active_server->shutdown_requested = 1;
        active_server->restart_requested = 1;
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

    if (install_shutdown_handlers() != 0) {
        perror("sigaction");
        return 1;
    }

    for (;;) {
        ServerConfig config;
        Server server;
        int restart;

        runtime_config_defaults(&config);
        if (runtime_config_load(&config, path) != 0) {
            if (argc > 1 || errno != ENOENT) {
                fprintf(stderr, "Failed to load configuration: %s\n", path);
                return 1;
            }
            clearerr(stderr);
        }

        if (ensure_databases(&config) != 0) return 1;

        if (server_init(&server, &config) != 0) {
            fprintf(stderr, "Failed to start %s on port %s\n",
                    config.server_name, config.port);
            return 1;
        }
        server.started_at = time(NULL);

        /* GeoLite2 files are optional. Missing files leave Client.geoip unavailable. */
        if (geoip_init(&server.geoip, config.geoip_city_db, config.geoip_asn_db) != 0) {
            server_destroy(&server);
            fprintf(stderr, "Failed to initialize GeoIP subsystem\n");
            return 1;
        }

        printf("%s (%s) listening on port %s with %zu listener(s)\n",
               config.server_name, IRCD_VERSION, config.port,
               server.listener_count);
        active_server = &server;
        server_run(&server);
        active_server = NULL;
        restart = server.restart_requested && !server.shutdown_requested;
        channel_log_flush_all(&server);
        geoip_destroy(&server.geoip);
        server_destroy(&server);

        if (!restart) break;
        fprintf(stdout, "Restarting ScratchIRCd using %s\n", path);
    }

    return 0;
}