#include "runtime_config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int load_single_option(const char *line) {
    char path[] = "/tmp/scratchircd-config-limit-XXXXXX";
    int fd = mkstemp(path);
    FILE *file;
    ServerConfig config;
    int rc;
    assert(fd >= 0);
    file = fdopen(fd, "w");
    assert(file != NULL);
    assert(fputs(line, file) >= 0);
    assert(fclose(file) == 0);
    runtime_config_defaults(&config);
    rc = runtime_config_load(&config, path);
    (void)unlink(path);
    return rc;
}

static int load_server_name_length(size_t length) {
    char line[IRC_SERVER_NAME_MAX + 32U];
    size_t prefix = strlen("server_name = ");
    assert(length + prefix + 2U <= sizeof(line));
    memcpy(line, "server_name = ", prefix);
    memset(line + prefix, 's', length);
    line[prefix + length] = '\n';
    line[prefix + length + 1U] = '\0';
    return load_single_option(line);
}

int main(int argc, char **argv) {
    char path[] = "/tmp/scratchircd-config-XXXXXX";
    int fd = mkstemp(path);
    FILE *file;
    ServerConfig config;

    assert(argc == 1 || argc == 2);

    runtime_config_defaults(&config);
    assert(config.max_channels == IRCD_DEFAULT_MAX_CHANNELS);
    assert(config.ping_interval_seconds == IRCD_DEFAULT_PING_INTERVAL_SECONDS);
    assert(config.ping_timeout_seconds == IRCD_DEFAULT_PING_TIMEOUT_SECONDS);
    assert(config.history_retention_days == IRCD_DEFAULT_HISTORY_RETENTION_DAYS);
    assert(config.history_max_rows == IRCD_DEFAULT_HISTORY_MAX_ROWS);
    assert(config.channel_log_queue_max_rows == IRCD_DEFAULT_CHANNEL_LOG_QUEUE_MAX_ROWS);
    assert(config.memoserv_sender_quota == IRCD_DEFAULT_MEMOSERV_SENDER_QUOTA);
    assert(config.nickserv_registrations_per_ip == IRCD_DEFAULT_NICKSERV_REGISTRATIONS_PER_IP);
    assert(config.nickserv_registration_window_seconds == IRCD_DEFAULT_NICKSERV_REGISTRATION_WINDOW_SECONDS);
    assert(config.nickserv_mail_requests_per_ip == IRCD_DEFAULT_NICKSERV_MAIL_REQUESTS_PER_IP);
    assert(config.nickserv_mail_window_seconds == IRCD_DEFAULT_NICKSERV_MAIL_WINDOW_SECONDS);
    assert(config.nickserv_mail_global_per_minute == IRCD_DEFAULT_NICKSERV_MAIL_GLOBAL_PER_MINUTE);
    assert(config.argon2_ops_per_ip == IRCD_DEFAULT_ARGON2_OPS_PER_IP);
    assert(config.argon2_window_seconds == IRCD_DEFAULT_ARGON2_WINDOW_SECONDS);
    assert(config.argon2_global_ops_per_minute == IRCD_DEFAULT_ARGON2_GLOBAL_OPS_PER_MINUTE);
    assert(config.argon2_global_burst_per_second == IRCD_DEFAULT_ARGON2_GLOBAL_BURST_PER_SECOND);
    assert(config.tls_chain_file[0] == '\0');

    assert(fd >= 0);
    file = fdopen(fd, "w");
    assert(file != NULL);
    assert(fputs("max_channels = 8192\n", file) >= 0);
    assert(fputs("max_connections_per_ip = 4\n", file) >= 0);
    assert(fputs("connection_limit_exempt_ip = 192.0.2.10\n", file) >= 0);
    assert(fputs("connection_limit_exempt_ip = 2001:db8::10\n", file) >= 0);
    assert(fputs("registration_timeout_seconds = 75\n", file) >= 0);
    assert(fputs("ping_interval_seconds = 45\n", file) >= 0);
    assert(fputs("ping_timeout_seconds = 30\n", file) >= 0);
    assert(fputs("output_queue_max_bytes = 32768\n", file) >= 0);
    assert(fputs("cloak_prefix = dru\n", file) >= 0);
    assert(fputs("cloak_key = runtime-test-cloak-key-0123456789\n", file) >= 0);
    assert(fputs("tls_chain_file = /etc/scratchircd/intermediates.pem\n", file) >= 0);
    assert(fputs("dnsbl_timeout_seconds = 9\n", file) >= 0);
    assert(fputs("dnsbl = Primary dnsbl.example.test\n", file) >= 0);
    assert(fputs("dnsbl = Secondary dnsbl2.example.test\n", file) >= 0);
    assert(fputs("chanserv_db = data/test-chanserv.db\n", file) >= 0);
    assert(fputs("memoserv_db = data/test-memoserv.db\n", file) >= 0);
    assert(fputs("memoserv_quota = 77\n", file) >= 0);
    assert(fputs("memoserv_sender_quota = 333\n", file) >= 0);
    assert(fputs("memoserv_retention_days = 45\n", file) >= 0);
    assert(fputs("history_db = data/test-history.db\n", file) >= 0);
    assert(fputs("history_limit = 42\n", file) >= 0);
    assert(fputs("history_retention_days = 60\n", file) >= 0);
    assert(fputs("history_max_rows = 500000\n", file) >= 0);
    assert(fputs("channel_log_queue_max_rows = 600000\n", file) >= 0);
    assert(fputs("nickserv_registrations_per_ip = 7\n", file) >= 0);
    assert(fputs("nickserv_registration_window_seconds = 7200\n", file) >= 0);
    assert(fputs("nickserv_mail_requests_per_ip = 9\n", file) >= 0);
    assert(fputs("nickserv_mail_window_seconds = 1800\n", file) >= 0);
    assert(fputs("nickserv_mail_global_per_minute = 120\n", file) >= 0);
    assert(fputs("argon2_ops_per_ip = 12\n", file) >= 0);
    assert(fputs("argon2_window_seconds = 120\n", file) >= 0);
    assert(fputs("argon2_global_ops_per_minute = 180\n", file) >= 0);
    assert(fputs("argon2_global_burst_per_second = 25\n", file) >= 0);
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
    assert(config.max_channels == 8192U);
    assert(config.max_connections_per_ip == 4U);
    assert(config.connection_limit_exempt_ip_count == 2U);
    assert(strcmp(config.connection_limit_exempt_ips[0], "192.0.2.10") == 0);
    assert(strcmp(config.connection_limit_exempt_ips[1], "2001:db8::10") == 0);
    assert(config.registration_timeout_seconds == 75U);
    assert(config.ping_interval_seconds == 45U);
    assert(config.ping_timeout_seconds == 30U);
    assert(config.output_queue_max_bytes == 32768U);
    assert(strcmp(config.cloak_prefix, "dru") == 0);
    assert(strcmp(config.cloak_key, "runtime-test-cloak-key-0123456789") == 0);
    assert(strcmp(config.tls_chain_file,
                  "/etc/scratchircd/intermediates.pem") == 0);
    assert(config.dnsbl_timeout_seconds == 9U);
    assert(config.dnsbl_count == 2U);
    assert(strcmp(config.dnsbls[0].name, "Primary") == 0);
    assert(strcmp(config.dnsbls[0].zone, "dnsbl.example.test") == 0);
    assert(strcmp(config.dnsbls[1].name, "Secondary") == 0);
    assert(strcmp(config.dnsbls[1].zone, "dnsbl2.example.test") == 0);
    assert(strcmp(config.chanserv_db, "data/test-chanserv.db") == 0);
    assert(strcmp(config.memoserv_db, "data/test-memoserv.db") == 0);
    assert(config.memoserv_quota == 77U);
    assert(config.memoserv_sender_quota == 333U);
    assert(config.memoserv_retention_days == 45U);
    assert(strcmp(config.history_db, "data/test-history.db") == 0);
    assert(config.history_limit == 42U);
    assert(config.history_retention_days == 60U);
    assert(config.history_max_rows == 500000U);
    assert(config.channel_log_queue_max_rows == 600000U);
    assert(config.nickserv_registrations_per_ip == 7U);
    assert(config.nickserv_registration_window_seconds == 7200U);
    assert(config.nickserv_mail_requests_per_ip == 9U);
    assert(config.nickserv_mail_window_seconds == 1800U);
    assert(config.nickserv_mail_global_per_minute == 120U);
    assert(config.argon2_ops_per_ip == 12U);
    assert(config.argon2_window_seconds == 120U);
    assert(config.argon2_global_ops_per_minute == 180U);
    assert(config.argon2_global_burst_per_second == 25U);
    assert(config.kline_default_duration_seconds == 1800U);
    assert(strcmp(config.kline_default_reason, "default kline reason") == 0);
    assert(config.zline_default_duration_seconds == 900U);
    assert(strcmp(config.zline_default_reason, "default zline reason") == 0);
    assert(strcmp(config.sendmail_path, "/usr/sbin/sendmail") == 0);
    assert(strcmp(config.mail_from, "services@example.test") == 0);
    assert(config.nickserv_reset_seconds == 1200U);
    assert(config.nickserv_verify_seconds == 7200U);

    assert(load_server_name_length(IRC_SERVER_NAME_MAX) == 0);
    assert(load_server_name_length(IRC_SERVER_NAME_MAX + 1U) != 0);
    assert(load_single_option("max_channels = 0\n") != 0);
    assert(load_single_option("max_channels = 262145\n") != 0);
    assert(load_single_option("max_channels = 262144\n") == 0);
    assert(load_single_option("ping_interval_seconds = 0\n") != 0);
    assert(load_single_option("ping_interval_seconds = 3601\n") != 0);
    assert(load_single_option("ping_interval_seconds = 3600\n") == 0);
    assert(load_single_option("ping_timeout_seconds = 0\n") != 0);
    assert(load_single_option("ping_timeout_seconds = 3601\n") != 0);
    assert(load_single_option("ping_timeout_seconds = 3600\n") == 0);
    assert(load_single_option("history_retention_days = 3651\n") != 0);
    assert(load_single_option("history_retention_days = 0\n") == 0);
    assert(load_single_option("history_max_rows = 0\n") != 0);
    assert(load_single_option("history_max_rows = 10000001\n") != 0);
    assert(load_single_option("history_max_rows = 10000000\n") == 0);
    assert(load_single_option("channel_log_queue_max_rows = 0\n") != 0);
    assert(load_single_option("channel_log_queue_max_rows = 5000001\n") != 0);
    assert(load_single_option("channel_log_queue_max_rows = 5000000\n") == 0);
    assert(load_single_option("memoserv_sender_quota = 0\n") != 0);
    assert(load_single_option("memoserv_sender_quota = 50001\n") != 0);
    assert(load_single_option("memoserv_sender_quota = 50000\n") == 0);
    assert(load_single_option("nickserv_registrations_per_ip = 0\n") == 0);
    assert(load_single_option("nickserv_registrations_per_ip = 101\n") != 0);
    assert(load_single_option("nickserv_registration_window_seconds = 59\n") != 0);
    assert(load_single_option("nickserv_registration_window_seconds = 86401\n") != 0);
    assert(load_single_option("nickserv_registration_window_seconds = 86400\n") == 0);
    assert(load_single_option("nickserv_mail_requests_per_ip = 0\n") == 0);
    assert(load_single_option("nickserv_mail_requests_per_ip = 101\n") != 0);
    assert(load_single_option("nickserv_mail_window_seconds = 59\n") != 0);
    assert(load_single_option("nickserv_mail_window_seconds = 86401\n") != 0);
    assert(load_single_option("nickserv_mail_window_seconds = 86400\n") == 0);
    assert(load_single_option("nickserv_mail_global_per_minute = 0\n") != 0);
    assert(load_single_option("nickserv_mail_global_per_minute = 1001\n") != 0);
    assert(load_single_option("nickserv_mail_global_per_minute = 1000\n") == 0);
    assert(load_single_option("argon2_ops_per_ip = 0\n") == 0);
    assert(load_single_option("argon2_ops_per_ip = 101\n") != 0);
    assert(load_single_option("argon2_ops_per_ip = 100\n") == 0);
    assert(load_single_option("argon2_window_seconds = 9\n") != 0);
    assert(load_single_option("argon2_window_seconds = 3601\n") != 0);
    assert(load_single_option("argon2_window_seconds = 3600\n") == 0);
    assert(load_single_option("argon2_global_ops_per_minute = 0\n") == 0);
    assert(load_single_option("argon2_global_ops_per_minute = 1001\n") != 0);
    assert(load_single_option("argon2_global_ops_per_minute = 1000\n") == 0);
    assert(load_single_option("argon2_global_burst_per_second = 0\n") == 0);
    assert(load_single_option("argon2_global_burst_per_second = 101\n") != 0);
    assert(load_single_option("argon2_global_burst_per_second = 100\n") == 0);
    assert(load_single_option("chanserv_max_channels_per_account = 20\n") != 0);

    (void)unlink(path);

    if (argc == 2) {
        runtime_config_defaults(&config);
        assert(runtime_config_load(&config, argv[1]) == 0);
    }

    return 0;
}
