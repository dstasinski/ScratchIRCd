# ScratchIRCd Network Administrator Guide

This guide documents the commands and responsibilities available to the ScratchIRCd network administrator. It is updated as features are implemented.

## Client identity and security policy

Every connected client has exactly three host/address identity fields:

- `real_ip` — actual end-user numeric IP address.
- `real_host` — FCrDNS-verified hostname for that actual IP, when available.
- `display_host` — the only hostname disclosed through normal IRC client-visible protocol output.

WHO, ordinary WHOIS, USERHOST, channel/user prefixes, and channel ban masks use `display_host`. Vhosts (`+t`) replace only `display_host`; planned cloaking (`+x`) will do the same.

KLINE and ZLINE are deliberately independent of public cloaks/vhosts. KLINE checks `user@real_host` when a verified hostname exists and `user@real_ip`; ZLINE uses only `real_ip`. Network administrators and IRC operators can inspect real identity via operator WHOIS output and USERIP.

For future WebIRC connections, the real fields will contain the authenticated end user's identity, not the gateway address. Any retained gateway audit data will be maintained separately from the Client identity fields.

## Bootstrap network administrator

The network administrator is the only operator identity stored in `ircd.conf`. Ordinary IRC operators live in `data/operators.db`.

```text
operators_db = data/operators.db
bans_db = data/bans.db
netadmin_name = root
netadmin_password_hash = $argon2id$...
netadmin_hostmask = *!*@*
netadmin_vhost = admin.example.net
```

Generate the Argon2id password hash with:

```sh
./build/scratchircd-mkpasswd 'your password'
```

Authenticate with:

```text
OPER root <password>
```

A successful bootstrap login receives network-administrator mode `+N` and the complete operator permission set. The bootstrap hostmask is evaluated against the client's real resolved hostname and real IP, never a cloak or vhost.

## Operator database management

Ordinary operator accounts are stored in `data/operators.db`. Plaintext passwords supplied through IRC are immediately converted to Argon2id hashes.

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

`-` means no vhost or no permissions. Database operators may not receive `netadmin`.

## Persistent server bans

KLINE and ZLINE records are stored in `data/bans.db` and survive server restarts.

### KLINE

```text
KLINE <user@host-mask> :<reason>
KLINE -<user@host-mask>
```

Adding a KLINE requires `can_kline`. Removing one requires `can_unkline`. Wildcards `*` and `?` are supported. Matching checks `user@real_host` when available and `user@real_ip`; `display_host` is not considered. Existing matching clients are disconnected immediately and new matching clients are rejected before registration.

### ZLINE

```text
ZLINE <ip-mask> :<reason>
ZLINE -<ip-mask>
```

ZLINE requires `can_zline`. It matches `real_ip`, supports `*` and `?`, persists in `data/bans.db`, disconnects currently matching clients, and rejects future matching connections before registration.

## Implemented administrative/operator commands

### KILL

```text
KILL <nickname> :<reason>
```

Requires `can_kill`.

### WALLOPS

```text
WALLOPS :<message>
```

Requires `can_wallops`.

### REHASH

```text
REHASH
```

Requires `can_rehash`. Reloads runtime configuration that can safely change without rebuilding listeners.

### RESTART

```text
RESTART
```

Requires `can_restart`. Disconnects current clients, reloads the active configuration, recreates listeners/databases, and starts a fresh Server instance in the same process.

### SAJOIN / SAPART

```text
SAJOIN <nick> <channel>[,<channel>...]
SAPART <nick> <channel>[,<channel>...]
```

Require `can_override`. SAJOIN bypasses normal JOIN restrictions; SAPART forcibly removes the target from listed channels.

### SAMODE

```text
SAMODE <nick> <modes>
SAMODE <channel> <modes> [parameters...]
```

Requires `can_override`. Channel SAMODE uses normal MODE semantics with server authority. User SAMODE cannot create provenance/security modes such as `+N`, `+o`, `+r`, `+S`, `+t`, `+V`, `+x`, or `+z`.

### SETHOST / SETIDENT / SETNAME

```text
SETHOST <nick> <newhost>
SETIDENT <nick> <newident>
SETNAME <nick> :<new real name>
```

Require `can_override`. SETHOST changes only `display_host`, sets `+t`, and leaves `real_ip`/`real_host` untouched.

### USERIP and operator WHOIS

`USERIP` is operator-only and returns `real_ip`. Operator WHOIS includes numeric 378 with `real_host` (or IP fallback) and `real_ip`.

## Operator permissions

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — use RESTART.
- `helpop` — grants user mode `+h` on OPER login.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs.
- `get_host` — apply the configured operator vhost to `display_host` and grant `+t`.
- `can_override` — use SAJOIN, SAPART, SAMODE, SETHOST, SETIDENT, and SETNAME.
- `netadmin` — bootstrap network administrator only.

## Commands currently available to the network administrator

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
WHO
WHOIS
ZLINE
```

## Planned DNSBL and GeoIP connection policy

DNSBL and GeoIP will be attached to the finalized `real_ip` after direct/WebIRC identity is established and before registration completes. DNSBL queries will be asynchronous; configured blacklist hits may automatically create persistent ZLINE records in `data/bans.db` and reject the connection.

GeoIP will use libmaxminddb directly with downloaded `GeoLite2-City.mmdb` and `GeoLite2-ASN.mmdb` files under `data/`. Client GeoIP state will expose: `status`, `ip`, `network`, `source`, `continent_code`, `country_code`, `country_name`, `region_code`, `region_name`, `city`, `asn`, and `organization`. These fields will be kept in a dedicated nested Client GeoIP structure rather than mixed into the three host identity fields.

## Security and runtime data

All ScratchIRCd databases belong under `data/`. Current databases are:

```text
data/operators.db
data/bans.db
```

Future service/history databases and MaxMind `.mmdb` files will use the same runtime-data directory convention. `ircd.conf`, runtime databases, SQLite journal/WAL files, and `build/` are excluded from source control.
