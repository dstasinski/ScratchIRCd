#include "runtime_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static int load_reserved_nicks(const char *line, ServerConfig *config) {
    char path[] = "/tmp/scratchircd-reserved-nicks-XXXXXX";
    int fd = mkstemp(path);
    FILE *file;
    int rc;

    assert(fd >= 0);
    file = fdopen(fd, "w");
    assert(file != NULL);
    assert(fputs(line, file) >= 0);
    assert(fclose(file) == 0);

    runtime_config_defaults(config);
    rc = runtime_config_load(config, path);
    (void)unlink(path);
    return rc;
}

static int has_reserved_nick(const ServerConfig *config, const char *nick) {
    size_t i;

    for (i = 0U; i < config->reserved_nick_count; ++i) {
        if (strcasecmp(config->reserved_nicks[i], nick) == 0) return 1;
    }
    return 0;
}

int main(void) {
    ServerConfig config;

    assert(load_reserved_nicks("reserved_nicks = Admin,admin,ADMIN,Root,ROOT,,OperServ\n",
                               &config) == 0);
    assert(config.reserved_nick_count == 3U);
    assert(strcmp(config.reserved_nicks[0], "Admin") == 0);
    assert(strcmp(config.reserved_nicks[1], "Root") == 0);
    assert(strcmp(config.reserved_nicks[2], "OperServ") == 0);
    assert(has_reserved_nick(&config, "admin"));
    assert(has_reserved_nick(&config, "ADMIN"));
    assert(has_reserved_nick(&config, "root"));
    assert(has_reserved_nick(&config, "OPERSERV"));

    assert(load_reserved_nicks("reserved_nicks = 1Admin\n", &config) != 0);
    assert(load_reserved_nicks("reserved_nicks = Admin!\n", &config) != 0);

    return 0;
}
