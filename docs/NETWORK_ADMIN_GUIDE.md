# ScratchIRCd Network Administrator Guide

This is the technical guide for installing, building, configuring, running, upgrading, backing up, and troubleshooting ScratchIRCd. IRC client and operator command references are in `CLIENT_GUIDE.md` and `OPERATOR_GUIDE.md`.

## Platform and dependencies

ScratchIRCd is a single-server Linux IRC daemon written in C11. It requires:

- CMake 3.16 or newer.
- GCC or Clang and the normal C build toolchain.
- POSIX threads.
- Python 3 for integration tests and soak tooling.
- OpenSSL, SQLite3, Argon2, and MaxMindDB development libraries.

On Debian or Ubuntu:

```sh
sudo apt update
sudo apt install build-essential cmake python3 libargon2-dev libsqlite3-dev libssl-dev openssl libmaxminddb-dev
```

Install the `sqlite3` command-line program as well if you plan to inspect, back up, restore, or vacuum databases manually:

```sh
sudo apt install sqlite3
```

## Clone and select Genesis

```sh
git clone https://github.com/dstasinski/ScratchIRCd.git
cd ScratchIRCd
git switch Genesis
```

For an existing clean checkout:

```sh
git switch Genesis
git pull --ff-only origin Genesis
```

Do not update a production checkout with uncommitted changes. Use `git status --short` before pulling.

## Compile and test

### Strict release build

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The primary outputs are:

- `build/scratchircd` — the daemon.
- `build/scratchircd-mkpasswd` — the Argon2id password-hash utility.

### Debug sanitizer build

Clang with AddressSanitizer and UndefinedBehaviorSanitizer is recommended before deployment:

```sh
CC=clang cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON \
  -DSCRATCHIRCD_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-sanitize --output-on-failure
```

### Operational soak test

After all normal tests pass, run the release-candidate soak against a release build:

```sh
python3 tools/run_soak.py build/scratchircd \
  --duration-hours 12 \
  --clients 12 \
  --release-candidate \
  --report "soak-$(git rev-parse --short HEAD).json"
```

The report is a deployment artifact; do not commit it unless intentionally preserving release evidence.

## Install and initial run

Create the runtime configuration and writable directories:

```sh
cp ircd.conf.example ircd.conf
mkdir -p data logs
```

Edit `ircd.conf` before starting. At minimum, set the server identity, administrator contact, listener policy, a private cloak key, and bootstrap network-administrator credentials.

Start from the repository root so relative paths resolve correctly:

```sh
./build/scratchircd ./ircd.conf
```

To install binaries under the configured CMake prefix, normally `/usr/local`:

```sh
sudo cmake --install build
/usr/local/bin/scratchircd /absolute/path/to/ircd.conf
```

When using an installed binary, prefer absolute paths in `ircd.conf` for certificates, text files, databases, GeoIP files, logs, and the sendmail program.

## Configuration file

ScratchIRCd reads `key = value` lines. Blank lines and lines beginning with `#` are ignored. Duplicate list-style options such as `webirc_gateway`, `connection_limit_exempt_ip`, and `dnsbl` add entries; other keys replace a single value. Begin with `ircd.conf.example`, which tracks every supported setting and its defaults.

### Server identity, listeners, and limits

```text
server_name = irc.example.net
network_name = ExampleNet
bind_address =
port = 6667
tls_port = 6697
max_clients = 1024
max_channels = 4096
max_connections_per_ip = 4
connection_limit_exempt_ip = 192.0.2.10
registration_timeout_seconds = 60
output_queue_max_bytes = 65536
```

An empty `bind_address` uses the normal wildcard listeners. `max_connections_per_ip = 0` disables that limit. Trusted WebIRC gateway source IPs are automatically exempt; the supplied end-user IP is checked after WebIRC authentication. The output queue is a bounded SendQ, and a slow client is disconnected if it exceeds the configured bytes.

Current protocol limits are compiled into `include/config.h`:

```c
#define IRC_NICK_MAX 15U
#define IRC_USER_MAX 10U
#define IRC_CHANNEL_NAME_MAX 32U
```

### Administrator contact, MOTD, and rules

```text
motd_file = motd.txt
rules_file = rules.txt
admin_location1 = Example IRC Network
admin_location2 = Phoenix, Arizona, USA
admin_email = irc-admin@example.net
```

`admin_location1` and `admin_location2` are the two descriptive location/contact lines returned by `/ADMIN`. A useful convention is the organization or network name on the first line and the city/region/country on the second. They are free-form text and do not affect routing or security.

### TLS

```text
tls_port = 6697
tls_cert_file = /etc/letsencrypt/live/irc.example.net/fullchain.pem
tls_key_file = /etc/letsencrypt/live/irc.example.net/privkey.pem
```

TLS is enabled only when both paths are present. The daemon requires TLS 1.2 or newer. The service account must be able to read the key without making it world-readable. Successful TLS clients receive `+z`.

Test the certificate and listener after startup:

```sh
openssl s_client -connect irc.example.net:6697 -servername irc.example.net
```

### Cloaking and client identity

Every registered client receives mode `+x`. Generate a private cloak key of at least 16 characters:

```sh
openssl rand -hex 32
```

```text
cloak_prefix = example
cloak_key = <private-random-value>
```

An empty cloak key leaves the displayed hostname unchanged even though `+x` is present. Do not rotate the key casually: it changes displayed identities and therefore affects channel-ban masks.

ScratchIRCd maintains three identity values:

- `real_ip` — the end user's numeric IP.
- `real_host` — the forward-confirmed reverse-DNS hostname when one is verified.
- `display_host` — the only hostname disclosed through ordinary IRC output.

KLINE uses real host/IP identity; ZLINE, DNSBL, GeoIP, and connection limits use the real IP. Vhosts and cloaks modify only `display_host`.

### Connection liveness and no-spoof

```text
ping_interval_seconds = 90
ping_timeout_seconds = 90
nospoof = yes
nospoof_timeout_seconds = 30
dns_timeout_seconds = 5
```

After `ping_interval_seconds` without a complete inbound command, the server sends a PING. Only the exact matching PONG clears the outstanding challenge; the client is disconnected after `ping_timeout_seconds` otherwise. This liveness clock is separate from WHOIS messaging-idle time, which resets only after a private or channel PRIVMSG is successfully delivered.

With no-spoof enabled, registration also requires an exact PONG response to the initial random cookie. The server then sends a CTCP VERSION request. Until the client returns its VERSION in a NOTICE addressed to the server name, channel joins and ordinary message/notice delivery are restricted; traffic to operators and network administrators remains available for help. WebIRC clients may also receive a CTCP WEBSITE request.

### WebIRC gateways

Authorize each gateway by its numeric TCP peer IP and a unique password:

```text
webirc_gateway = 127.0.0.1 gateway-secret
webirc_gateway = 2001:db8::10 another-secret
```

The gateway sends before registration:

```text
WEBIRC gateway-secret gateway.example client.example 203.0.113.25
```

Gateway DNS names are not used for authorization. After successful authentication, the supplied client IP becomes `real_ip`; gateway audit identity remains separate. Use TLS or another protected link between the gateway and server so the shared password is not exposed.

### DNS blacklists

```text
dnsbl_timeout_seconds = 5
dnsbl = Spamhaus zen.spamhaus.org
dnsbl = DroneBL dnsbl.dronebl.org
```

Checks are asynchronous and use the final direct or WebIRC end-user IP. A positive result creates an exact-IP ZLINE. Resolver failure and timeout fail open. Confirm that your DNSBL provider permits your resolver and intended query volume.

### MaxMind GeoIP and GeoBAN

```text
geoip_city_db = data/GeoLite2-City.mmdb
geoip_asn_db = data/GeoLite2-ASN.mmdb
```

The MMDB files are optional and are not distributed with ScratchIRCd. Keep them current using your licensed MaxMind download process. COUNTRY and REGION GeoBAN values are normalized to uppercase, ASN accepts numeric or `AS`-prefixed values, and ORG uses case-insensitive Tcl-style wildcard matching against the MaxMind organization string.

### Server password and service mail

```text
server_password =
sendmail_path = /usr/sbin/sendmail
mail_from = irc-services@example.net
nickserv_reset_seconds = 1800
nickserv_verify_seconds = 86400
```

An empty `server_password` permits registration without PASS. NickServ verification and reset email require a usable `sendmail_path` and sender address. Test mail delivery and monitor the local mail-transfer agent's logs before advertising account recovery.

### Resource, retention, and fairness controls

```text
history_limit = 100
history_retention_days = 30
history_max_rows = 250000
channel_log_queue_max_rows = 250000
memoserv_quota = 100
memoserv_sender_quota = 500
memoserv_retention_days = 90
nickserv_registrations_per_ip = 5
nickserv_registration_window_seconds = 3600
nickserv_mail_requests_per_ip = 5
nickserv_mail_window_seconds = 900
nickserv_mail_global_per_minute = 60
argon2_ops_per_ip = 10
argon2_window_seconds = 60
argon2_global_ops_per_minute = 60
argon2_global_burst_per_second = 8
```

The row ceilings remain effective even if age-based retention is disabled. A zero value disables only those fair-share controls whose comments in `ircd.conf.example` explicitly allow it; compile-time global ceilings still apply. See `STORAGE_LIMITS.md` for hard limits and detailed policy.

### Ban defaults

Nickname shorthand for KLINE and ZLINE uses these temporary-policy defaults:

```text
kline_default_duration_seconds = 3600
kline_default_reason = Temporary KLINE
zline_default_duration_seconds = 3600
zline_default_reason = Temporary ZLINE
```

Explicit masks remain permanent until removed.

## Bootstrap network administrator

Only the bootstrap network administrator is configured in `ircd.conf`:

```text
netadmin_name = root
netadmin_password_hash = $argon2id$...
netadmin_hostmask = *!*@trusted.example
netadmin_vhost = admin.example.net
```

Generate the password hash locally:

```sh
./build/scratchircd-mkpasswd 'choose-a-strong-password'
```

Copy only the resulting Argon2id hash into `netadmin_password_hash`. Restrict `netadmin_hostmask` to real trusted identity whenever practical. Authentication uses:

```text
OPER root choose-a-strong-password
```

The bootstrap account receives `+N` and all operator permissions. Use it to create narrowly permitted day-to-day operator accounts, then reserve the bootstrap credentials for administration.

## SQLite databases

Configure writable paths:

```text
operators_db = data/operators.db
bans_db = data/bans.db
nickserv_db = data/nickserv.db
chanserv_db = data/chanserv.db
memoserv_db = data/memoserv.db
history_db = data/history.db
```

| File | Contents |
| --- | --- |
| `operators.db` | IRC operator credentials, permissions, vhosts, and enabled state. |
| `bans.db` | KLINE, ZLINE, and GeoBAN policy. |
| `nickserv.db` | NickServ accounts, Argon2id hashes, email state, tokens, and account vhosts. |
| `chanserv.db` | Registered channels, founders, access roles, persistent modes/topics, logging state, and durable log queue. |
| `memoserv.db` | Account-to-account memos. |
| `history.db` | Bounded IRCv3 channel message history. |

The daemon creates missing files and required tables when it opens them and applies its supported schema upgrades. Do not create application tables by hand. All paths and their parent directories must be writable by the service account.

ScratchIRCd uses SQLite WAL mode. A live database can therefore have `-wal` and `-shm` companions; they are part of its current state and must not be omitted from a raw file copy.

### Consistent backups

The safest simple procedure is to stop the daemon, copy `ircd.conf`, all configured database files, MOTD/rules, TLS configuration references, and any local channel logs, then restart. If the daemon must remain online, use SQLite's backup command for each database instead of copying only the main file:

```sh
mkdir -p backup
sqlite3 data/operators.db ".backup 'backup/operators.db'"
sqlite3 data/bans.db ".backup 'backup/bans.db'"
sqlite3 data/nickserv.db ".backup 'backup/nickserv.db'"
sqlite3 data/chanserv.db ".backup 'backup/chanserv.db'"
sqlite3 data/memoserv.db ".backup 'backup/memoserv.db'"
sqlite3 data/history.db ".backup 'backup/history.db'"
```

Protect backups as secrets: they contain account and operator password hashes, email addresses, reset/verification state, private messages, and network policy. Test restoration periodically on a separate system.

### Restore

Stop the daemon before replacing databases. Verify ownership and permissions, keep the old files until the restored service passes testing, and never combine a restored main database with unrelated old `-wal` or `-shm` files.

For a backup produced by `.backup`, restore into a new path first:

```sh
sqlite3 restored-nickserv.db ".restore 'backup/nickserv.db'"
sqlite3 restored-nickserv.db "PRAGMA integrity_check;"
```

After `integrity_check` reports `ok`, place the restored file at the configured path while the daemon is stopped and set the correct service ownership and restrictive permissions.

### Integrity checks and offline VACUUM

Run checks during a maintenance window or against backups:

```sh
sqlite3 data/nickserv.db "PRAGMA quick_check;"
sqlite3 data/chanserv.db "PRAGMA quick_check;"
```

Deleting rows does not necessarily shrink a SQLite file because freed pages are normally reused. To return unused pages to the filesystem, back up first, stop the daemon, and run an offline `VACUUM` on the selected database:

```sh
sqlite3 data/history.db "VACUUM;"
```

Never run `VACUUM` in the message hot path or as an uncontrolled live maintenance job.

## Channel text logging

ChanServ channel logging uses a durable queue before periodic text-file flushes. Enable or disable it through an authenticated IRC operator or network administrator:

```text
CHANSERV SET #chat LOGGING ON
CHANSERV SET #chat LOGGING OFF
```

The service account must be able to create and append files under `logs/`. If the durable queue reaches `channel_log_queue_max_rows`, new log events are refused until the backlog can flush, and operators receive rate-limited notices. Monitor filesystem availability, free space, ownership, and permissions.

## REHASH versus RESTART

`REHASH` parses the original configuration file and rejects changes that cannot be applied safely to live state. A restart is required when changing:

- Bind address, plaintext or TLS port, certificate/key path, server name, or GeoIP database path.
- Any configured SQLite database path.
- No-spoof settings, registration timeout, PING interval/timeout, SendQ ceiling, or maximum channel count.
- Server password, cloak prefix/key, or a reduction of `max_clients` below the live client count.

Other validated changes can be reloaded when the operator has `can_rehash`:

```text
REHASH
```

Use the daemon command for a graceful in-process restart when the operator has `can_restart`:

```text
RESTART
```

For binary upgrades, use an external service manager: stop the old process cleanly and start the new binary after tests pass.

## Service supervision and automatic restart

Use a dedicated unprivileged account and a service manager such as systemd. The following is a starting point; replace all paths and the account name for the installation:

```ini
[Unit]
Description=ScratchIRCd
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=scratchircd
Group=scratchircd
WorkingDirectory=/srv/scratchircd
ExecStart=/usr/local/bin/scratchircd /srv/scratchircd/ircd.conf
Restart=on-failure
RestartSec=5s
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ReadWritePaths=/srv/scratchircd/data /srv/scratchircd/logs

[Install]
WantedBy=multi-user.target
```

Save it as `/etc/systemd/system/scratchircd.service`, verify the configured user can read the configuration/certificates and write only the necessary data/log directories, then enable it:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now scratchircd.service
sudo systemctl status scratchircd.service
journalctl -u scratchircd.service -f
```

`Restart=on-failure` restarts crashes and nonzero exits without turning an intentional clean shutdown into a restart loop. Rate limiting can be added with `StartLimitIntervalSec` and `StartLimitBurst` if repeated startup failure is possible.

## Upgrade procedure

Use a clean checkout and keep the previous tested binary available for rollback:

```sh
git switch Genesis
git pull --ff-only origin Genesis
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
sudo cmake --install build
sudo systemctl restart scratchircd.service
sudo systemctl status scratchircd.service
```

Before the restart, take a consistent backup and review changes to `ircd.conf.example` for new or changed settings. Afterward, verify plaintext/TLS listeners as applicable, registration, SASL, joins, messaging, services, operator login, database writes, logs, and PING/PONG behavior.

## Security checklist

- Run the daemon as a dedicated unprivileged user, never as root.
- Keep `ircd.conf`, TLS private keys, `cloak_key`, and all databases non-world-readable.
- Use a long random cloak key and unique strong operator, WebIRC, and server passwords.
- Restrict the bootstrap network-administrator hostmask and create least-privilege operator accounts.
- Expose only intended listener ports through the firewall.
- Protect WebIRC connections and shared secrets in transit.
- Keep the OS, OpenSSL, SQLite, Argon2, MaxMind data, and compiler toolchain patched.
- Retain encrypted backups and test restoration.
- Monitor disk space, process restarts, channel-log backlog notices, authentication throttles, and ban changes.
- Run strict builds, the full test suite, sanitizer tests, and a soak before release deployment.

## Troubleshooting

### Configuration fails to load

Run the daemon in the foreground and read the reported file and line number. Confirm the line uses `key = value`, the key exists in `ircd.conf.example`, numeric values are in range, and `cloak_key` is empty or at least 16 characters.

### Listener will not start

Check whether another process owns the port and whether the configured bind address exists locally:

```sh
ss -ltnp
```

Ports below 1024 require additional privilege; prefer unprivileged IRC ports or an appropriate systemd capability policy.

### TLS will not start

Confirm both certificate paths are present, readable by the service user, and contain a matching certificate/key pair:

```sh
openssl x509 -in /path/to/fullchain.pem -noout -subject -dates
openssl pkey -in /path/to/privkey.pem -check -noout
```

### Database errors

Check the configured parent directory, ownership, filesystem free space, and whether stale files were copied incorrectly. Run `PRAGMA quick_check` against a backup or during a maintenance window. Preserve the main file and any WAL/SHM companions before attempting recovery.

### REHASH is rejected

The configuration parsed successfully but contains a startup-bound, persistent-store, or registration-gate change. Use a planned `RESTART` or external service restart.

### Clients time out

Verify clients return the exact PING token, and review `ping_interval_seconds`, `ping_timeout_seconds`, no-spoof settings, DNS timeouts, DNSBL responsiveness, firewall state, and SendQ disconnect notices.

### WebIRC identity is not accepted

Confirm the gateway's actual numeric peer IP exactly matches a `webirc_gateway` entry, the password matches, and `WEBIRC` is sent before `NICK` and `USER`. Do not authorize by a hostname that can resolve to changing addresses.
