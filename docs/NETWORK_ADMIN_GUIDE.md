# ScratchIRCd Network Administrator Guide

This guide documents commands and responsibilities available to the ScratchIRCd network administrator.

## Client identity and security policy

Every connected client has exactly three host/address identity fields:

- `real_ip` — actual end-user numeric IP address.
- `real_host` — FCrDNS-verified hostname for that actual IP, when available.
- `display_host` — the only hostname disclosed through normal IRC client-visible protocol output.

WHO, ordinary WHOIS, USERHOST, channel/user prefixes, and channel ban masks use `display_host`. Vhosts (`+t`) replace only `display_host`; planned cloaking (`+x`) will do the same.

KLINE checks `user@real_host` when available and `user@real_ip`; ZLINE and DNSBL use only `real_ip`. Operators can inspect real identity via operator WHOIS and USERIP.

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

Authorization requires both the configured numeric gateway IP and matching password. Gateway DNS names are not used for authorization. After successful WEBIRC, gateway information is saved only in `Client.webirc` audit metadata, `real_ip` becomes the supplied end-user IP, asynchronous FCrDNS restarts for that address, `display_host` follows the actual client identity, and user mode `+V` is set.

A failed/unauthorized WEBIRC attempt disconnects before registration. Protect `ircd.conf` with suitable filesystem permissions and prefer TLS between remote gateways and ScratchIRCd.

## DNS blacklist enforcement

DNSBL checks are configured with repeatable entries. ScratchIRCd currently supports up to eight configured lists:

```text
dnsbl_timeout_seconds = 5
dnsbl = Spamhaus zen.spamhaus.org
dnsbl = DroneBL dnsbl.dronebl.org
```

The first field after `dnsbl =` is the descriptive name written into ban metadata; the second is the DNS blacklist zone. The check runs only after the final direct/WebIRC `real_ip` is established. IPv4 queries use reversed octets; IPv6 queries use reversed nibbles.

All DNSBL resolver calls run on a dedicated worker thread, so a slow external DNS service never blocks the IRC event loop. Registration remains pending while the check is outstanding. If the configured timeout expires, or if the request cannot be queued, ScratchIRCd fails open and continues registration. This prevents an external DNS outage from taking the IRC server offline.

A positive result automatically writes an exact-IP ZLINE to `data/bans.db` and rejects the client. For example, a hit might create metadata equivalent to:

```text
mask:   203.0.113.42
reason: DNSBL Spamhaus (zen.spamhaus.org)
set_by: DNSBL:Spamhaus
```

Because this is a normal persistent ZLINE record, an authorized operator can remove it with:

```text
ZLINE -203.0.113.42
```

A later hit for the same IP replaces the existing exact-IP record rather than creating duplicates. DNSBL configuration and timeout changes may be applied with REHASH; the lookup request already in progress retains the configuration snapshot it started with.

## MaxMind GeoIP / ASN

ScratchIRCd uses libmaxminddb directly. Download the databases from MaxMind and place them under `data/`, or configure alternate paths:

```text
geoip_city_db = data/GeoLite2-City.mmdb
geoip_asn_db = data/GeoLite2-ASN.mmdb
```

The files are optional. If one or both are absent, ScratchIRCd continues to start and `Client.geoip.status` reflects that data is unavailable or not found.

GeoIP lookup occurs immediately before registration, after the final direct/WebIRC `real_ip` and FCrDNS state are established. Each Client contains:

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

`status` is currently `ok`, `not_found`, `unavailable`, or `error`. `ip` is the finalized `real_ip`. `network` is the actual matched CIDR network. `source` identifies City, ASN, or both. All strings are copied into the Client record. Changing either GeoIP database path requires RESTART rather than REHASH.

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

ZLINE persists in `data/bans.db` and matches only `real_ip`. For WebIRC users this is the end-user IP, not the gateway IP. DNSBL-generated bans use this same persistent ZLINE table.

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

`SETHOST` changes only `display_host`; `USERIP` and operator WHOIS reveal real identity. REHASH reloads safely mutable configuration, including WebIRC and DNSBL definitions. Listener/TLS and GeoIP database-path changes require RESTART.

## Operator permissions

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — use RESTART.
- `helpop` — grants `+h` on OPER login.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs, including DNSBL-generated records.
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

## Security and runtime data

Runtime data belongs under `data/`:

```text
data/operators.db
data/bans.db
data/GeoLite2-City.mmdb
data/GeoLite2-ASN.mmdb
```

`ircd.conf`, runtime databases, SQLite journal/WAL files, downloaded MMDB files, and `build/` are excluded from source control.
