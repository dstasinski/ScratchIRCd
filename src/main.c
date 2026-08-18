#include "ban_db.h"
#include "config.h"
#include "operator_db.h"
#include "runtime_config.h"
#include "server.h"

#include <errno.h>
#include <stdio.h>

/** Program entry point. Usage: scratchircd [config-file] */
int main(int argc, char **argv) {
    ServerConfig config;
    Server server;
    OperatorDb operators;
    BanDb bans;
    const char *path = argc > 1 ? argv[1] : IRCD_DEFAULT_CONFIG_FILE;

    runtime_config_defaults(&config);

    if (runtime_config_load(&config, path) != 0) {
        if (argc > 1 || errno != ENOENT) {
            fprintf(stderr, "Failed to load configuration: %s\n", path);
            return 1;
        }
        clearerr(stderr);
    }

    /* Ensure persistent database files/schemas exist before accepting clients. */
    if (operator_db_open(&operators, config.operators_db) != 0) {
        fprintf(stderr, "Failed to open operator database: %s\n", config.operators_db);
        return 1;
    }
    operator_db_close(&operators);

    if (ban_db_open(&bans, config.bans_db) != 0) {
        fprintf(stderr, "Failed to open ban database: %s\n", config.bans_db);
        return 1;
    }
    ban_db_close(&bans);

    if (server_init(&server, &config) != 0) {
        fprintf(stderr, "Failed to start %s on port %s\n",
                config.server_name, config.port);
        return 1;
    }

    printf("%s (%s) listening on port %s with %zu listener(s)\n",
           config.server_name, IRCD_VERSION, config.port,
           server.listener_count);
    server_run(&server);
    server_destroy(&server);
    return 0;
}
