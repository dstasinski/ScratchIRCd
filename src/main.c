#include "ban_db.h"
#include "config.h"
#include "geoip.h"
#include "operator_db.h"
#include "runtime_config.h"
#include "server.h"

#include <errno.h>
#include <stdio.h>

static int ensure_databases(const ServerConfig *config) {
    OperatorDb operators;
    BanDb bans;

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
    return 0;
}

/** Program entry point. Usage: scratchircd [config-file] */
int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : IRCD_DEFAULT_CONFIG_FILE;

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

        /* GeoLite2 files are optional. Missing files leave Client.geoip unavailable. */
        if (geoip_init(&server.geoip, config.geoip_city_db, config.geoip_asn_db) != 0) {
            server_destroy(&server);
            fprintf(stderr, "Failed to initialize GeoIP subsystem\n");
            return 1;
        }

        printf("%s (%s) listening on port %s with %zu listener(s)\n",
               config.server_name, IRCD_VERSION, config.port,
               server.listener_count);
        server_run(&server);
        restart = server.restart_requested;
        geoip_destroy(&server.geoip);
        server_destroy(&server);

        if (!restart) break;
        fprintf(stdout, "Restarting ScratchIRCd using %s\n", path);
    }

    return 0;
}
