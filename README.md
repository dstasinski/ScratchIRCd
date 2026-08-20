# ScratchIRCd

ScratchIRCd is a Linux IRC daemon written from scratch in C. It is intentionally single-server and will never link to other IRC servers. Development currently happens directly on the `Genesis` branch.

## Current foundation

The daemon currently provides a C11/CMake build, dynamic clients, IPv4/IPv6 listeners, RFC1459 casemapping, `#` and `&` channels, asynchronous FCrDNS, OpenSSL TLS, authorized WebIRC gateways, runtime configuration, modular IRC commands, user/channel mode state, per-channel membership privileges, Argon2id operator authentication, and SQLite-backed operator/ban persistence.

## TLS

ScratchIRCd can expose plaintext and TLS listeners simultaneously. TLS uses OpenSSL and is enabled only when both certificate and private-key paths are configured:

```text
port = 6667
tls_port = 6697
tls_cert_file = /path/to/fullchain.pem
tls_key_file = /path/to/privkey.pem
```

TLS handshakes are advanced non-blockingly by the event loop. A client receives user mode `+z` only after a successful TLS handshake. Channel mode `+z` therefore accepts only genuinely encrypted clients. TLS configuration/listener changes require RESTART rather than REHASH.

## WebIRC

Trusted WebIRC gateways are configured by numeric peer IP and password. Repeat the option for multiple gateways:

```text
webirc_gateway = 127.0.0.1 gateway-secret
webirc_gateway = 2001:db8::10 another-secret
```

A gateway sends the standard pre-registration command:

```text
WEBIRC <password> <gateway-name> <supplied-hostname> <client-ip>
```

The gateway's TCP peer IP must match a configured numeric gateway address and the password must match. Gateway DNS names are never trusted for authorization. The supplied hostname is retained only as WebIRC audit metadata; ScratchIRCd performs its own asynchronous FCrDNS against the supplied client IP. Successful WEBIRC sets user mode `+V`.

Gateway audit metadata is deliberately separate from the normal Client identity fields. DNSBL, GeoIP, KLINE, ZLINE, operator WHOIS and USERIP will therefore operate on the actual WebIRC end-user identity, not the gateway.

## Client identity model

Every IRC client has exactly three address/host identity fields:

- `real_ip` — the actual end-user numeric IP address.
- `real_host` — the FCrDNS-verified hostname for `real_ip`, or empty when no verified hostname exists.
- `display_host` — the only host exposed through ordinary IRC protocol output.

For direct connections, `real_ip` is initialized from the accepted socket. For authenticated WebIRC connections, `real_ip` is replaced with the actual end-user address supplied by the trusted gateway and DNS is restarted against that address.

`display_host` initially falls back to `real_ip` and becomes `real_host` after successful FCrDNS. A vhost (`+t`) changes only `display_host`. The planned cloak mode (`+x`) will likewise change only `display_host`. WHO, ordinary WHOIS, USERHOST, channel/user message prefixes, JOIN/PART/QUIT, and channel ban masks use only `display_host`.

Server security uses the real fields: ZLINE uses `real_ip`; KLINE checks both `user@real_host` when available and `user@real_ip`. An IRC operator can inspect real identity through operator WHOIS output and USERIP. Ordinary clients cannot use USERIP and never receive real IP/hostname data through WHO/WHOIS.

## Runtime databases

All ScratchIRCd SQLite databases live under `data/`. Current databases are:

```text
data/operators.db
data/bans.db
```

Future NickServ, ChanServ, MemoServ, and IRCv3 history databases will use the same directory.

Only the bootstrap network administrator is configured in `ircd.conf`. Ordinary IRC operators live in `data/operators.db`, and persistent KLINE/ZLINE records live in `data/bans.db`.

## Network administrator and operators

Generate the bootstrap Argon2id password hash with:

```sh
./build/scratchircd-mkpasswd 'your password'
```

The network administrator manages ordinary operators with `OPERADD`, `OPERDEL`, `OPERSET`, and `OPERLIST`. Operator authority is stored in a separate permission bitset and is never inferred merely from user mode `+o`.

Permission-controlled commands include KILL, KLINE, ZLINE, WALLOPS, REHASH, RESTART, SAJOIN, SAPART, SAMODE, SETHOST, SETIDENT, and SETNAME. SETHOST changes only `display_host`; real security identity remains untouched.

## Documentation

User-facing command documentation is maintained separately by role:

- `docs/CLIENT_GUIDE.md` — ordinary client commands, user modes, and channel modes.
- `docs/OPERATOR_GUIDE.md` — ordinary IRC operator authentication, permissions, real-identity access, and commands.
- `docs/NETWORK_ADMIN_GUIDE.md` — bootstrap administration, operator management, persistent bans, override commands, TLS, WebIRC, and network-administrator commands.

## Currently implemented commands

`ADMIN`, `AWAY`, `INVITE`, `ISON`, `JOIN`, `KICK`, `KILL`, `KLINE`, `LIST`, `LUSERS`, `MODE`, `MOTD`, `NAMES`, `NICK`, `NOTICE`, `OPER`, `OPERADD`, `OPERDEL`, `OPERLIST`, `OPERSET`, `PART`, `PASS`, `PING`, `PRIVMSG`, `QUIT`, `REHASH`, `RESTART`, `RULES`, `SAJOIN`, `SAMODE`, `SAPART`, `SETHOST`, `SETIDENT`, `SETNAME`, `TOPIC`, `USER`, `USERHOST`, `USERIP` (operator-only), `WALLOPS`, `WEBIRC` (authorized gateways), `WHO`, `WHOIS`, and `ZLINE`.

## Dependencies

On Debian/Ubuntu systems:

```sh
sudo apt install build-essential cmake python3 libargon2-dev libsqlite3-dev libssl-dev openssl
```

## Building and testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CTest includes unit tests for client identity, modes, channel policy, visibility, operator permissions, operator database CRUD, and persistent bans, plus socket-level integration tests for core protocol behavior, operator actions, server-authority overrides/restart, TLS, and WebIRC.

## Planned connection policy: DNSBL and GeoIP

WebIRC now establishes the final end-user `real_ip` before registration. DNSBL and GeoIP can therefore be attached to the finalized real identity without evaluating a WebIRC gateway address.

Configured DNSBL zones will be queried asynchronously. A configured positive match can automatically create a persistent ZLINE in `data/bans.db` and reject the connection.

GeoIP will use libmaxminddb directly with downloaded `data/GeoLite2-City.mmdb` and `data/GeoLite2-ASN.mmdb` files. A dedicated nested Client GeoIP record is planned with the fields `status`, `ip`, `network`, `source`, `continent_code`, `country_code`, `country_name`, `region_code`, `region_name`, `city`, `asn`, and `organization`.

## Planned architecture

The long-term daemon will include SQLite-backed NickServ, ChanServ, MemoServ, and IRCv3 history; persistent ChanServ channels; SASL; hostname cloaking for `+x`; DNSBL enforcement; GeoIP/ASN enrichment and policy; complete client/channel mode behavior; full applicable ISUPPORT advertising; and the remaining planned standard command set.

Services will be addressable virtual identities but will never join channels or appear in ordinary client lists. Persistent channels will be restored from ChanServ state rather than requiring a service client in the channel.
