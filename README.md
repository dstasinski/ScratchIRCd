# ScratchIRCd

ScratchIRCd is a Linux IRC daemon written from scratch in C. It is intentionally single-server and will never link to other IRC servers. Development currently happens directly on the `Genesis` branch.

## Current foundation

The daemon currently provides a C11/CMake build, dynamic clients, IPv4/IPv6 listeners, RFC1459 casemapping, `#` and `&` channels, asynchronous FCrDNS, OpenSSL TLS, authorized WebIRC gateways, MaxMind GeoLite2 City/ASN enrichment, asynchronous DNSBL enforcement, IRCv3 CAP negotiation with SASL PLAIN and persistent channel history, runtime configuration, modular IRC commands, user/channel mode state, per-channel membership privileges, Argon2id operator/NickServ authentication, SQLite-backed operator/ban/account/history persistence, and a virtual NickServ service with nickname and email-based account recovery.

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
```

Clients that negotiate `batch` and `draft/chathistory` may request:

```text
CHATHISTORY LATEST <channel> * <limit>
```

The requester must currently be a member of the channel. Playback is returned inside a `chathistory` batch, and clients that also negotiate `server-time` receive the original UTC timestamp on every replayed record. ScratchIRCd advertises `MSGREFTYPES=timestamp` in ISUPPORT for this first history implementation.

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

Channel bans and normal WHO/WHOIS/user prefixes use `display_host`. KLINE/ZLINE, DNSBL, GeoIP, and operator security inspection use the real fields. Vhosts (`+t`) and future cloaking (`+x`) change only `display_host`.

History stores the displayed identity that was public when a channel message was sent; it never persists real IP/DNS identity in a replayable message record.

## DNS blacklist enforcement

DNSBL zones are configured with repeatable entries:

```text
dnsbl_timeout_seconds = 5
dnsbl = Spamhaus zen.spamhaus.org
dnsbl = DroneBL dnsbl.dronebl.org
```

ScratchIRCd supports up to eight configured lists. DNSBL queries run on a dedicated worker thread and never block the IRC event loop. The lookup begins only after the final direct/WebIRC `real_ip` has been established.

A positive result immediately creates an exact-IP persistent ZLINE in `data/bans.db` and rejects the connection. Resolver/submission failure or timeout fails open. DNSBL-created ZLINEs are normal ZLINE records and may be removed with `ZLINE -<ip>` by an authorized operator.

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

## Runtime data

All ScratchIRCd runtime databases and downloaded MaxMind files live under `data/`:

```text
data/operators.db
data/bans.db
data/nickserv.db
data/history.db
data/GeoLite2-City.mmdb
data/GeoLite2-ASN.mmdb
```

Future ChanServ and MemoServ databases will use the same directory.

## Documentation

- `docs/CLIENT_GUIDE.md` — ordinary client commands, CAP/SASL/history, and modes.
- `docs/IRCV3_GUIDE.md` — capability negotiation, account-notify, SASL, and persistent history.
- `docs/NICKSERV_GUIDE.md` — complete NickServ registration, SASL relationship, recovery and email-reset guide.
- `docs/OPERATOR_GUIDE.md` — IRC operator authentication, permissions, identity access, and commands.
- `docs/NETWORK_ADMIN_GUIDE.md` — bootstrap administration, operator/NickServ management, bans, TLS, WebIRC, GeoIP, DNSBL, SASL/history, and configuration.

## Currently implemented commands

`ADMIN`, `AUTHENTICATE`, `AWAY`, `CAP`, `CHATHISTORY`, `IDENTIFY`, `INVITE`, `ISON`, `JOIN`, `KICK`, `KILL`, `KLINE`, `LIST`, `LUSERS`, `MODE`, `MOTD`, `NAMES`, `NICK`, `NICKSERV`, `NOTICE`, `NSDROP`, `NSINFO`, `NSSET`, `OPER`, `OPERADD`, `OPERDEL`, `OPERLIST`, `OPERSET`, `PART`, `PASS`, `PING`, `PRIVMSG`, `QUIT`, `REHASH`, `RESTART`, `RULES`, `SAJOIN`, `SAMODE`, `SAPART`, `SETHOST`, `SETIDENT`, `SETNAME`, `TOPIC`, `USER`, `USERHOST`, `USERIP` (operator-only), `WALLOPS`, `WEBIRC` (authorized gateways), `WHO`, `WHOIS`, and `ZLINE`.

## Dependencies

On Debian/Ubuntu systems:

```sh
sudo apt install build-essential cmake python3 libargon2-dev libsqlite3-dev libssl-dev openssl libmaxminddb-dev
```

Email recovery does not add a compile-time dependency. A real deployment that enables it needs a configured sendmail-compatible local MTA or wrapper.

## Building and testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CTest includes unit tests for client identity, GeoIP, DNSBL, runtime configuration, modes, channel policy, visibility, operator permissions/databases, persistent bans, NickServ persistence, and history persistence. Socket-level integration tests cover core protocol behavior, operator actions/overrides, TLS, WebIRC, NickServ nickname/email recovery, IRCv3 SASL/capabilities, and history persistence across a daemon restart.

## Planned architecture

The long-term daemon will add ChanServ, MemoServ, persistent ChanServ channels, hostname cloaking for `+x`, broader CHATHISTORY reference modes and message IDs, additional IRCv3 capabilities, GeoIP/ASN-based policy, complete client/channel mode behavior, full applicable ISUPPORT advertising, and the remaining planned standard command set.

Services will remain addressable virtual identities that never join channels or appear in ordinary client lists. Persistent channels will be restored from ChanServ state rather than requiring a service client in the channel.
