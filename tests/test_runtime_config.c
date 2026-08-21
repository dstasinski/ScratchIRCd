#include "runtime_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/scratchircd-config-XXXXXX";
    int fd = mkstemp(path);
    FILE *file;
    ServerConfig config;

    assert(fd >= 0);
    file = fdopen(fd, "w");
    assert(file != NULL);
    assert(fputs("dnsbl_timeout_seconds = 9\n", file) >= 0);
    assert(fputs("dnsbl = Primary dnsbl.example.test\n", file) >= 0);
    assert(fputs("dnsbl = Secondary dnsbl2.example.test\n", file) >= 0);
    assert(fclose(file) == 0);

    runtime_config_defaults(&config);
    assert(runtime_config_load(&config, path) == 0);
    assert(config.dnsbl_timeout_seconds == 9U);
    assert(config.dnsbl_count == 2U);
    assert(strcmp(config.dnsbls[0].name, "Primary") == 0);
    assert(strcmp(config.dnsbls[0].zone, "dnsbl.example.test") == 0);
    assert(strcmp(config.dnsbls[1].name, "Secondary") == 0);
    assert(strcmp(config.dnsbls[1].zone, "dnsbl2.example.test") == 0);

    (void)unlink(path);
    return 0;
}
