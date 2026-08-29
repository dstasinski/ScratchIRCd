# ScratchIRCd

ScratchIRCd is a Linux IRC daemon written from scratch in C. It is intentionally single-server and will never link to other IRC servers. Development currently happens directly on the `Genesis` branch.

## Release status

The `Genesis` branch contains a broad development implementation. Implemented features are not automatically part of the supported release surface. The first release gate is [Milestone 1: Minimum Secure IRC Server](MILESTONE-1.md), which certifies a deliberately bounded core before advanced services and integrations are promoted into supported releases.

## Current foundation

The daemon currently provides a C11/CMake build, dynamic clients, IPv4/IPv6 listeners, RFC1459 casemapping, `#` and `&` channels, asynchronous FCrDNS, OpenSSL TLS, authorized WebIRC gateways, MaxMind GeoLite2 City/ASN enrichment, asynchronous DNSBL enforcement, IRCv3 CAP negotiation with SASL PLAIN and persistent channel history, runtime configuration, modular IRC commands, user/channel mode state, per-channel membership privileges, Argon2id operator/NickServ authentication, SQLite-backed operator/ban/account/channel/history persistence, a virtual NickServ service with nickname and email-based account recovery, and a virtual ChanServ service for registered persistent channels.

## Connection liveness

Registered clients that send no complete IRC command for 90 seconds receive a
server `PING`. The client must return the exact token in `PONG` within another
90 seconds or it is disconnected with `Ping Timeout: 90 seconds`. Other client
traffic does not substitute for the required PONG once a challenge is pending.

The release defaults may be changed between 1 and 3600 seconds:

```text
ping_interval_seconds = 90
ping_timeout_seconds = 90
```

## TLS

ScratchIRCd can expose plaintext and TLS listeners simultaneously:

```text
port = 6667
tls_port = 6697
tls_cert_file = /path/to/fullchain.pem
tls_key_file = /path/to/privkey.pem
```

TLS handshakes are non-blocking. User mode `+z` is granted only after a successful OpenSSL handshake, and channel mode `+z` therefore accepts only genuinely encrypted clients.

## IRCv3

ScratchIRCd currently advertises:

```text
account-notify batch draft/chathistory sasl server-time
```

SASL `PLAIN` authenticates against the NickServ account database. `account-notify` reports post-registration account changes to capable peers sharing a channel.

Channel `PRIVMSG` and `NOTICE` history is persisted in:

```text
history_db = data/history.db
history_limit = 100
history_retention_days = 30
history_max_rows = 250000
```

Age-based expiration may be disabled with `history_retention_days = 0`, but `history_max_rows` remains the global hard row ceiling. Maintenance is throttled rather than performed on every message.

A client needs `draft/chathistory` to request:

```text
CHATHISTORY LATEST <channel> * <limit>
```

The requester must currently be a member of the channel. `batch` optionally packages playback in a `chathistory` batch, and `server-time` supplies original UTC timestamps. ScratchIRCd advertises `MSGREFTYPES=timestamp` and the configured `CHATHISTORY=<limit>` value in ISUPPORT.

See `docs/IRCV3_GUIDE.md` for the current IRCv3 behavior and limitations.

## WebIRC

Trusted WebIRC gateways are configured by numeric peer IP and password:

```text
webirc_gateway = 127.0.0.1 gateway-secret
webirc_gateway = 2001:db8::10 another-secret
```

A gateway sends:

```text
WEBIRC <password> <gateway-name> <supplied-hostname> <client-ip>
```

Successful WEBIRC sets `+V`, stores gateway audit information separately, replaces `real_ip` with the end-user address, and restarts asynchronous FCrDNS for that end-user address. Gateway-supplied hostnames are audit metadata only.

## Client identity model

Every IRC client has exactly three normal address/host identity fields:

- `real_ip` — actual end-user numeric IP.
- `real_host` — FCrDNS-verified hostname for `real_ip`, when available.
- `display_host` — the only hostname exposed through normal IRC protocol output.

Channel bans and normal WHO/WHOIS/user prefixes use `display_host`. KLINE/ZLINE, DNSBL, GeoIP, and operator security inspection use the real fields. Vhosts (`+t`) and cloaking (`+x`) change only `display_host`.

History stores the displayed identity that was public when a channel message was sent; it never persists real IP/DNS identity in a replayable message record.

## DNS blacklist enforcement

DNSBL zones are configured with repeatable entries:

```text
dnsbl_timeout_seconds = 5
dnsbl = Spamhaus zen.spamhaus.org
dnsbl = DroneBL dnsbl.dronebl.org
```

ScratchIRCd supports up to eight configured lists. DNSBL queries run on a dedicated worker thread and never block the IRC event loop. The lookup begins only after the final direct/WebIRC `real_ip` has been established.

A positive result immediately creates an exact-IP timed ZLINE in `data/bans.db` and rejects the connection. The automatic ZLINE lifetime is `zline_default_duration_seconds`; expired rows are reclaimed by normal ban-database maintenance. Resolver/submission failure or timeout fails open. DNSBL-created ZLINEs may also be removed early with `ZLINE -<ip>` by an authorized operator.

## GeoIP / ASN enrichment

ScratchIRCd uses libmaxminddb directly with downloaded MaxMind databases:

```text
geoip_city_db = data/GeoLite2-City.mmdb
geoip_asn_db = data/GeoLite2-ASN.mmdb
```

The files are optional; a missing database does not prevent startup. Lookups occur once, immediately before registration after direct/WebIRC identity and FCrDNS are finalized, so the lookup always uses the actual end-user `real_ip`.

Each `Client` contains a nested `ClientGeoIP geoip` record with `status`, `ip`, `network`, `source`, continent/country/region/city fields, `asn`, and `organization`.

## NickServ accounts and recovery

Registered nicknames/accounts are stored in:

```text
nickserv_db = data/nickserv.db
```

The NickServ account table has a compile-time hard ceiling of 100,000 rows. Existing accounts remain usable at the ceiling; only creation of additional accounts is refused until capacity is freed.

NickServ is a virtual service identity, not a `Client`. It never joins channels and is never inserted into NAMES, WHO, ISON, LUSERS, or ordinary client hashes. `NickServ`, `ChanServ`, and `MemoServ` are reserved nicknames.

ScratchIRCd accepts both `PRIVMSG NickServ :...` and a direct `NICKSERV ...` command. Implemented NickServ subcommands include:

```text
REGISTER <password>
IDENTIFY [account] <password>
RECOVER <nick>
RECOVER <nick> KILL
GHOST <nick>
SET PASSWORD <new-password>
SET EMAIL <address>
VERIFY <token>
RESET <account>
RESET <account> <token> <new-password>
HELP
```

Default `RECOVER` safely renames a nickname squatter to a generated `Guest<connection-id>` nickname. `RECOVER ... KILL` disconnects the occupying session, and `GHOST` is a KILL alias. All three authorize against the authenticated NickServ account rather than IRC-operator permissions.

Successful account authentication stores the account name separately from the current nickname and grants user mode `+r`. NickServ passwords are stored only as Argon2id hashes. A NickServ vhost replaces only `display_host` and grants `+t`; `real_ip` and `real_host` remain untouched.

### Email verification and password reset

Email recovery is optional. Configure a local sendmail-compatible MTA:

```text
sendmail_path = /usr/sbin/sendmail
mail_from = services@example.net
nickserv_reset_seconds = 1800
nickserv_verify_seconds = 86400
```

`SET EMAIL` sends a verification token. The address cannot be used for password recovery until `VERIFY` succeeds. Verification and reset tokens are random and only their SHA-256 hashes are stored in SQLite. Password-reset requests deliberately return the same generic response whether or not an account exists, reducing account/email enumeration. Reset tokens are time-limited and single-use.

ScratchIRCd invokes the configured sendmail-compatible binary directly rather than through a shell, and delivery is detached from the IRC event loop. Email delivery is disabled when either `sendmail_path` or `mail_from` is empty.

## ChanServ persistent channels

Registered channels are stored in:

```text
chanserv_db = data/chanserv.db
```

Persistent ChanServ storage is capped at 50,000 registered channels globally and 256 explicit access entries per channel. Each persistent ban, exception, and invite-exception list also inherits the IRC live-list maximum of 100 entries.

ChanServ is a virtual service with server authority. It never joins channels and never appears in ordinary client lists. Users may address it with either `CHANSERV ...` or `PRIVMSG ChanServ :...`.

The initial service commands are:

```text
REGISTER <#channel> [:description]
INFO <#channel>
DROP <#channel>
HELP
```

`REGISTER` and `DROP` are restricted to network administrators (`+N`) through both ChanServ command syntaxes. Registration also requires the network administrator to be identified to NickServ and to hold owner/operator privilege in the live channel. The issuing account initially becomes founder and the channel receives service-controlled `+r`. A network administrator may then delegate normal channel ownership with `CSSET <#channel> FOUNDER <NickServ-account>`; the delegated founder manages ChanServ access and channel policy but still cannot REGISTER or DROP channels. When the channel later disappears from memory or the daemon restarts, the SQLite registration remains; on the next JOIN ScratchIRCd restores `+r`. An authenticated founder automatically receives `+q/+o` when joining the registered channel, even under a different current nickname.

Network administrators additionally have `CSINFO`, `CSSET`, and `CSDROP` management commands. Numeric 005 advertises the ScratchIRCd-specific `PCHANNELS=` token listing enabled registered channels.

See `docs/CHANSERV_GUIDE.md` for the complete current ChanServ behavior.

## Persistent storage growth

ScratchIRCd bounds client-amplifiable persistent data. In addition to the history and service limits above, the durable channel-log staging queue defaults to 250,000 rows and refuses new log events rather than deleting existing backlog when full. Daily channel log files are retained for 90 days. MemoServ uses both recipient quotas and age retention. Operator-created permanent KLINE/ZLINE/GeoBAN policy remains permanent by design.

See `docs/STORAGE_LIMITS.md` for the complete storage-growth policy and operational notes.

## Runtime data

All ScratchIRCd runtime databases and downloaded MaxMind files live under `data/`, while optional channel text logs live under `logs/`:

```text
data/operators.db
data/bans.db
data/nickserv.db
data/chanserv.db
data/memoserv.db
data/history.db
data/GeoLite2-City.mmdb
data/GeoLite2-ASN.mmdb
logs/<channel>.log.<date>
```

## Documentation

- `docs/CLIENT_GUIDE.md` — ordinary client commands, CAP/SASL/history, and modes.
- `docs/IRCV3_GUIDE.md` — capability negotiation, account-notify, SASL, and persistent history.
- `docs/NICKSERV_GUIDE.md` — complete NickServ registration, SASL relationship, recovery and email-reset guide.
- `docs/CHANSERV_GUIDE.md` — registered-channel persistence, founder authority, PCHANNELS, and administrator commands.
- `docs/OPERATOR_GUIDE.md` — IRC operator authentication, permissions, identity access, and commands.
- `docs/NETWORK_ADMIN_GUIDE.md` — bootstrap administration, operator/service management, bans, TLS, WebIRC, GeoIP, DNSBL, SASL/history, and configuration.
- `docs/STORAGE_LIMITS.md` — persistent-storage ceilings, retention behavior, and disk-management notes.

## Currently implemented commands

`ADMIN`, `AUTHENTICATE`, `AWAY`, `CAP`, `CHANSERV`, `CHATHISTORY`, `CSDROP`, `CSINFO`, `CSSET`, `DEAF`, `DIE`, `GEOBAN`, `GLOBOPS`, `IDENTIFY`, `INVITE`, `ISON`, `JOIN`, `KICK`, `KILL`, `KLINE`, `KNOCK`, `LINKS`, `LIST`, `LOCOPS`, `LUSERS`, `MEMOSERV`, `MODE`, `MOTD`, `MSINFO`, `MSPURGE`, `MUTE`, `NAMES`, `NICK`, `NICKSERV`, `NOTICE`, `NSDROP`, `NSINFO`, `NSSET`, `OPER`, `OPERADD`, `OPERDEL`, `OPERLIST`, `OPERSET`, `PART`, `PASS`, `PING`, `PONG`, `PRIVMSG`, `QUIT`, `REHASH`, `RESTART`, `RULES`, `SAJOIN`, `SAMODE`, `SAPART`, `SETHOST`, `SETIDENT`, `SETNAME`, `SILENCE`, `SNOTICE`, `STATS`, `TIME`, `TOPIC`, `UNGEOBAN`, `USER`, `USERHOST`, `USERIP`, `VERSION`, `WALLOPS`, `WATCH`, `WEBIRC`, `WHO`, `WHOIS`, `WHOWAS`, and `ZLINE`.

## Dependencies

On Debian/Ubuntu systems:

```sh
sudo apt install build-essential cmake python3 libargon2-dev libsqlite3-dev libssl-dev openssl libmaxminddb-dev
```

Email recovery does not add a compile-time dependency. A real deployment that enables it needs a configured sendmail-compatible local MTA or wrapper.

## Updating a manually started server

After downloading `update-and-restart.sh` once, run it from anywhere inside the repository:

```sh
./update-and-restart.sh
```

The script requires a clean `Genesis` worktree, updates by fast-forward only, rebuilds with strict compiler warnings, runs the complete test suite, installs under `/usr/local`, and gracefully restarts a single manually running `scratchircd` process with its original arguments and working directory. It refuses to guess when multiple daemon processes are running and never force-kills a daemon that fails to stop cleanly.

Use `./update-and-restart.sh --help` for build, installation-prefix, timeout, and log-path overrides. If no daemon is running, the script builds, tests, and installs without starting one because it has no safe configuration command to infer.

## Building and testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
