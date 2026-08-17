#include "config.h"
#include "server.h"

#include <stdio.h>

/**
 * Program entry point.
 *
 * Usage: simple-ircd [port]
 * The default bind address and port are compile-time options in config.h.
 */
int main(int argc, char **argv) {
    const char *port = argc > 1 ? argv[1] : IRCD_DEFAULT_PORT;
    Server server;

    if (server_init(&server, IRCD_DEFAULT_BIND_ADDRESS, port) < 0) {
        fprintf(stderr, "Failed to start %s on port %s\n",
                IRCD_SERVER_NAME, port);
        return 1;
    }

    printf("%s (%s) listening on port %s\n",
           IRCD_SERVER_NAME, IRCD_VERSION, port);
    server_run(&server);
    server_destroy(&server);
    return 0;
}
