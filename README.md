# ScratchIRCd

ScratchIRCd is a Linux IRC daemon written from scratch in C. It is intentionally single-server and will never link to other IRC servers. Development currently happens directly on the `Genesis` branch.

## Current foundation

The daemon currently provides a C11/CMake build, dynamic clients, IPv4/IPv6 listeners, RFC1459 casemapping, `#` and `&` channels, asynchronous FCrDNS, OpenSSL TLS, authorized WebIRC gateways, MaxMind GeoLite2 City/ASN enrichment, runtime configuration, modular IRC commands, user/channel mode state, per-channel membership privileges, Argon2id operator authentication, and SQLite-backed operator/ban persistence.

## TLS

ScratchIRCd can expose plaintext and TLS listeners simultaneously:

```text
port = 6667
tls_port = 6697
tls_cert_file = /path/to/fullchain.pem
tls_key_file = /path/to/privkey.pem
```

TLS handshakes are non-blocking. User mode `+z` is granted only after a successful OpenSSL handshake, and channel mode `+z` therefore accepts only genuinely encrypted clients.

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

Channel bans and normal WHO/WHOIS/user prefixes use `display_host`. KLINE/ZLINE and operator security inspection use the real fields. Vhosts (`+t`) and future cloaking (`+x`) change only `display_host`.

## GeoIP / ASN enrichment

ScratchIRCd uses libmaxminddb directly with downloaded MaxMind databases:

```text
geoip_city_db = data/GeoLite2-City.mmdb
geoip_asn_db = data/GeoLite2-ASN.mmdb
```

The files are optional; a missing database does not prevent startup. Lookups occur once, immediately before registration after direct/WebIRC identity and FCrDNS are finalized, so the lookup always uses the actual end-user `real_ip`.

Each `Client` contains a nested `ClientGeoIP geoip` record with:

```text
status
ip
network
source
continent_code
country_code
country_name
region_code
region_name
city
asn
organization
```

`network` is the actual matched CIDR network from libmaxminddb. `source` records whether City, ASN, or both databases supplied data. MMDB strings are copied into Client storage; no Client field points into the memory-mapped database. GeoIP database path changes require RESTART.

## Runtime data

All ScratchIRCd runtime databases and downloaded MaxMind files live under `data/`:

```text
data/operators.db
data/bans.db
data/GeoLite2-City.mmdb
data/GeoLite2-ASN.mmdb
```

Future NickServ, ChanServ, MemoServ, and IRCv3 history databases will use the same directory.

## Documentation

- `docs/CLIENT_GUIDE.md` — ordinary client commands and modes.
- `docs/OPERATOR_GUIDE.md` — IRC operator authentication, permissions, identity access, and commands.
- `docs/NETWORK_ADMIN_GUIDE.md` — bootstrap administration, operator management, bans, TLS, WebIRC, GeoIP, and configuration.

## Currently implemented commands

`ADMIN`, `AWAY`, `INVITE`, `ISON`, `JOIN`, `KICK`, `KILL`, `KLINE`, `LIST`, `LUSERS`, `MODE`, `MOTD`, `NAMES`, `NICK`, `NOTICE`, `OPER`, `OPERADD`, `OPERDEL`, `OPERLIST`, `OPERSET`, `PART`, `PASS`, `PING`, `PRIVMSG`, `QUIT`, `REHASH`, `RESTART`, `RULES`, `SAJOIN`, `SAMODE`, `SAPART`, `SETHOST`, `SETIDENT`, `SETNAME`, `TOPIC`, `USER`, `USERHOST`, `USERIP` (operator-only), `WALLOPS`, `WEBIRC` (authorized gateways), `WHO`, `WHOIS`, and `ZLINE`.

## Dependencies

On Debian/Ubuntu systems:

```sh
sudo apt install build-essential cmake python3 libargon2-dev libsqlite3-dev libssl-dev openssl libmaxminddb-dev
```

## Building and testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CTest includes unit tests for client identity, GeoIP fallback state, modes, channel policy, visibility, operator permissions, operator database CRUD, and persistent bans, plus socket-level integration tests for protocol behavior, operator actions/overrides, TLS, and WebIRC.

## Next connection policy: DNSBL

With WebIRC and GeoIP now attached to finalized `real_ip`, configured DNSBL zones can be added to the same pre-registration policy stage. DNSBL queries will be asynchronous and configured positive matches can automatically create persistent ZLINE records in `data/bans.db` and reject the connection.

## Planned architecture

The long-term daemon will include SQLite-backed NickServ, ChanServ, MemoServ, and IRCv3 history; persistent ChanServ channels; SASL; hostname cloaking for `+x`; DNSBL enforcement; GeoIP/ASN-based policy; complete client/channel mode behavior; full applicable ISUPPORT advertising; and the remaining planned standard command set.

Services will be addressable virtual identities but will never join channels or appear in ordinary client lists. Persistent channels will be restored from ChanServ state rather than requiring a service client in the channel.
