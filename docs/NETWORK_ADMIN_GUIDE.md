# ScratchIRCd Network Administrator Guide

This guide documents commands and responsibilities available to the ScratchIRCd network administrator.

## Client identity and security policy

Every connected client has exactly three host/address identity fields:

- `real_ip` — actual end-user numeric IP address.
- `real_host` — FCrDNS-verified hostname for that actual IP, when available.
- `display_host` — the only hostname disclosed through normal IRC client-visible protocol output.

WHO, ordinary WHOIS, USERHOST, channel/user prefixes, and channel ban masks use `display_host`. Vhosts (`+t`) replace only `display_host`; planned cloaking (`+x`) will do the same.

KLINE checks `user@real_host` when available and `user@real_ip`; ZLINE uses only `real_ip`. Operators can inspect real identity via operator WHOIS and USERIP.

## TLS configuration

```text
port = 6667
tls_port = 6697
tls_cert_file = /etc/letsencrypt/live/irc.example.net/fullchain.pem
tls_key_file = /etc/letsencrypt/live/irc.example.net/privkey.pem
```

TLS is enabled only when both certificate and private-key paths are configured. TLS 1.2 is the minimum accepted protocol version. Successful TLS clients receive `+z`. TLS listener/certificate changes require RESTART.

## WebIRC gateways

Authorized gateways are configured in `ircd.conf` by numeric TCP peer IP and password. Repeat the setting to authorize multiple gateways:

```text
webirc_gateway = 127.0.0.1 gateway-secret
webirc_gateway = 2001:db8::10 another-secret
```

The gateway sends, before registration:

```text
WEBIRC <password> <gateway-name> <supplied-hostname> <client-ip>
```

Authorization requires both the configured numeric gateway IP and matching password. Gateway DNS names are not used for authorization.

After successful WEBIRC, gateway information is saved only in `Client.webirc` audit metadata, `real_ip` becomes the supplied end-user IP, asynchronous FCrDNS restarts for that address, `display_host` follows the actual client identity, and user mode `+V` is set.

A failed/unauthorized WEBIRC attempt disconnects before registration. Protect `ircd.conf` with suitable filesystem permissions and prefer TLS between remote gateways and ScratchIRCd.

## MaxMind GeoIP / ASN

ScratchIRCd uses libmaxminddb directly. Download the databases from MaxMind and place them under `data/`, or configure alternate paths:

```text
geoip_city_db = data/GeoLite2-City.mmdb
geoip_asn_db = data/GeoLite2-ASN.mmdb
```

The files are optional. If one or both are absent, ScratchIRCd continues to start and `Client.geoip.status` reflects that data is unavailable or not found.

GeoIP lookup occurs immediately before registration, after the final direct/WebIRC `real_ip` and FCrDNS state are established. WebIRC gateways are therefore never accidentally geolocated as the end user.

Each Client contains the nested fields:

```text
geoip.status
geoip.ip
geoip.network
geoip.source
geoip.continent_code
geoip.country_code
geoip.country_name
geoip.region_code
geoip.region_name
geoip.city
geoip.asn
geoip.organization
```

`status` is currently `ok`, `not_found`, `unavailable`, or `error`. `ip` is the finalized `real_ip`. `network` is the actual matched CIDR network reported by libmaxminddb. `source` identifies `GeoLite2-City`, `GeoLite2-ASN`, or both. Geographic names use the English (`en`) entry when present. `asn` is numeric and `organization` comes from the ASN database.

All strings are copied into the Client record. No Client field holds a pointer into the memory-mapped MMDB files. Changing either GeoIP database path requires RESTART rather than REHASH.

The downloaded `data/*.mmdb` files are excluded from Git so MaxMind datasets are not committed to the repository.

## Bootstrap network administrator

Only the bootstrap network administrator is stored in `ircd.conf`. Ordinary IRC operators live in `data/operators.db`.

```text
operators_db = data/operators.db
bans_db = data/bans.db
netadmin_name = root
netadmin_password_hash = $argon2id$...
netadmin_hostmask = *!*@*
netadmin_vhost = admin.example.net
```

Generate the hash with:

```sh
./build/scratchircd-mkpasswd 'your password'
```

Authenticate with:

```text
OPER root <password>
```

A successful bootstrap login receives `+N` and the complete operator permission set. The bootstrap hostmask is evaluated against real identity, never `display_host` or a WebIRC gateway.

## Operator database management

```text
OPERADD <name> <password> <vhost|-> :<permissions|->
OPERDEL <name>
OPERSET <name> NAME <newname>
OPERSET <name> PASSWORD <newpassword>
OPERSET <name> PERMISSIONS :<permissions|->
OPERSET <name> VHOST <vhost|->
OPERSET <name> ENABLED <0|1>
OPERLIST
OPERLIST <name>
```

Ordinary operators are stored in `data/operators.db`; plaintext OPER passwords are immediately converted to Argon2id hashes. Database operators cannot receive `netadmin`.

## Persistent server bans

### KLINE

```text
KLINE <user@host-mask> :<reason>
KLINE -<user@host-mask>
```

KLINE persists in `data/bans.db`, matches real host/IP identity, and ignores cloaks/vhosts.

### ZLINE

```text
ZLINE <ip-mask> :<reason>
ZLINE -<ip-mask>
```

ZLINE persists in `data/bans.db` and matches only `real_ip`. For WebIRC users this is the end-user IP, not the gateway IP.

## Implemented administrative/operator commands

```text
KILL <nickname> :<reason>
WALLOPS :<message>
REHASH
RESTART
SAJOIN <nick> <channel>[,<channel>...]
SAPART <nick> <channel>[,<channel>...]
SAMODE <nick> <modes>
SAMODE <channel> <modes> [parameters...]
SETHOST <nick> <newhost>
SETIDENT <nick> <newident>
SETNAME <nick> :<new real name>
USERIP <nick1> [nick2 ...]
WHOIS <nickname>
```

`SETHOST` changes only `display_host`; `USERIP` and operator WHOIS reveal real identity. REHASH reloads safely mutable configuration. Listener/TLS and GeoIP database-path changes require RESTART.

## Operator permissions

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — use RESTART.
- `helpop` — grants `+h` on OPER login.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs.
- `get_host` — apply the configured operator vhost and grant `+t`.
- `can_override` — use SAJOIN, SAPART, SAMODE, SETHOST, SETIDENT, and SETNAME.
- `netadmin` — bootstrap network administrator only.

## Complete implemented command set available to the network administrator

```text
ADMIN
AWAY
INVITE
ISON
JOIN
KICK
KILL
KLINE
LIST
LUSERS
MODE
MOTD
NAMES
NICK
NOTICE
OPER
OPERADD
OPERDEL
OPERLIST
OPERSET
PART
PASS
PING
PONG
PRIVMSG
QUIT
REHASH
RESTART
RULES
SAJOIN
SAMODE
SAPART
SETHOST
SETIDENT
SETNAME
TOPIC
USER
USERHOST
USERIP
WALLOPS
WEBIRC
WHO
WHOIS
ZLINE
```

`WEBIRC` is normally emitted by an authorized gateway rather than typed by an administrator, but it is part of the implemented protocol command set.

## Planned DNSBL connection policy

DNSBL will attach to finalized `real_ip` at the same pre-registration policy stage now used by GeoIP. DNSBL checks will be asynchronous; configured hits may automatically create persistent ZLINE records in `data/bans.db`.

## Security and runtime data

Runtime data belongs under `data/`:

```text
data/operators.db
data/bans.db
data/GeoLite2-City.mmdb
data/GeoLite2-ASN.mmdb
```

`ircd.conf`, runtime databases, SQLite journal/WAL files, downloaded MMDB files, and `build/` are excluded from source control.
