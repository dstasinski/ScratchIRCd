#include "config.h"
#include "runtime_config.h"
#include "server.h"

#include <errno.h>
#include <stdio.h>

/**
 * Program entry point.
 *
 * Usage: scratchircd [config-file]
 *
 * Defaults are always loaded first.  The default ircd.conf is optional so a
 * fresh build remains easy to run; an explicitly named configuration file is
 * required to load successfully.
 */
int main(int argc, char **argv) {
    ServerConfig config;
    Server server;
    const char *path = argc > 1 ? argv[1] : IRCD_DEFAULT_CONFIG_FILE;

    runtime_config_defaults(&config);

    if (runtime_config_load(&config, path) != 0) {
        if (argc > 1 || errno != ENOENT) {
            fprintf(stderr, "Failed to load configuration: %s\n", path);
            return 1;
        }
        clearerr(stderr);
    }

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
