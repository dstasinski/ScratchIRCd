#include "runtime_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
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
    assert(fputs("max_connections_per_ip = 4\n", file) >= 0);
    assert(fputs("connection_limit_exempt_ip = 192.0.2.10\n", file) >= 0);
    assert(fputs("connection_limit_exempt_ip = 2001:db8::10\n", file) >= 0);
    assert(fputs("dnsbl_timeout_seconds = 9\n", file) >= 0);
    assert(fputs("dnsbl = Primary dnsbl.example.test\n", file) >= 0);
    assert(fputs("dnsbl = Secondary dnsbl2.example.test\n", file) >= 0);
    assert(fputs("chanserv_db = data/test-chanserv.db\n", file) >= 0);
    assert(fputs("memoserv_db = data/test-memoserv.db\n", file) >= 0);
    assert(fputs("memoserv_quota = 77\n", file) >= 0);
    assert(fputs("memoserv_retention_days = 45\n", file) >= 0);
    assert(fputs("history_db = data/test-history.db\n", file) >= 0);
    assert(fputs("history_limit = 42\n", file) >= 0);
    assert(fputs("kline_default_duration_seconds = 1800\n", file) >= 0);
    assert(fputs("kline_default_reason = default kline reason\n", file) >= 0);
    assert(fputs("zline_default_duration_seconds = 900\n", file) >= 0);
    assert(fputs("zline_default_reason = default zline reason\n", file) >= 0);
    assert(fputs("sendmail_path = /usr/sbin/sendmail\n", file) >= 0);
    assert(fputs("mail_from = services@example.test\n", file) >= 0);
    assert(fputs("nickserv_reset_seconds = 1200\n", file) >= 0);
    assert(fputs("nickserv_verify_seconds = 7200\n", file) >= 0);
    assert(fclose(file) == 0);

    runtime_config_defaults(&config);
    assert(runtime_config_load(&config, path) == 0);
    assert(config.max_connections_per_ip == 4U);
    assert(config.connection_limit_exempt_ip_count == 2U);
    assert(strcmp(config.connection_limit_exempt_ips[0], "192.0.2.10") == 0);
    assert(strcmp(config.connection_limit_exempt_ips[1], "2001:db8::10") == 0);
    assert(config.dnsbl_timeout_seconds == 9U);
    assert(config.dnsbl_count == 2U);
    assert(strcmp(config.dnsbls[0].name, "Primary") == 0);
    assert(strcmp(config.dnsbls[0].zone, "dnsbl.example.test") == 0);
    assert(strcmp(config.dnsbls[1].name, "Secondary") == 0);
    assert(strcmp(config.dnsbls[1].zone, "dnsbl2.example.test") == 0);
    assert(strcmp(config.chanserv_db, "data/test-chanserv.db") == 0);
    assert(strcmp(config.memoserv_db, "data/test-memoserv.db") == 0);
    assert(config.memoserv_quota == 77U);
    assert(config.memoserv_retention_days == 45U);
    assert(strcmp(config.history_db, "data/test-history.db") == 0);
    assert(config.history_limit == 42U);
    assert(config.kline_default_duration_seconds == 1800U);
    assert(strcmp(config.kline_default_reason, "default kline reason") == 0);
    assert(config.zline_default_duration_seconds == 900U);
    assert(strcmp(config.zline_default_reason, "default zline reason") == 0);
    assert(strcmp(config.sendmail_path, "/usr/sbin/sendmail") == 0);
    assert(strcmp(config.mail_from, "services@example.test") == 0);
    assert(config.nickserv_reset_seconds == 1200U);
    assert(config.nickserv_verify_seconds == 7200U);

    (void)unlink(path);
    return 0;
}
