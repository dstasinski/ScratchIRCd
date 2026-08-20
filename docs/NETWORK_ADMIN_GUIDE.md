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

Example:

```text
KLINE baduser@*.example.net :abuse
KLINE -*@203.0.113.*
```

### ZLINE

```text
ZLINE <ip-mask> :<reason>
ZLINE -<ip-mask>
```

ZLINE requires `can_zline`. It matches `real_ip`, supports `*` and `?`, persists in `data/bans.db`, disconnects currently matching clients, and rejects future matching connections before registration.

## Other implemented operator commands

### KILL

```text
KILL <nickname> :<reason>
```

Requires `can_kill`. Ordinary operators cannot KILL a network administrator, and services-protected clients cannot be killed through ordinary KILL.

### WALLOPS

```text
WALLOPS :<message>
```

Requires `can_wallops`. The message is delivered to registered clients with user mode `+w`.

### REHASH

```text
REHASH
```

Requires `can_rehash`. ScratchIRCd reloads its current configuration file. Changes to listener address, port, server name, or an invalid reduction of `max_clients` are rejected and require a restart instead.

### USERIP

```text
USERIP <nick1> [nick2 ...]
```

Operator-only. Returns the target's `real_ip` rather than the displayed hostname.

### WHOIS real identity

Network administrators and IRC operators receive numeric 378 in WHOIS, containing the target's verified `real_host` (or IP fallback) and `real_ip`. Ordinary users receive only `display_host` through standard WHOIS.

## Operator permissions

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — permission for RESTART when implemented.
- `helpop` — grants user mode `+h` on OPER login.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs.
- `get_host` — apply the configured operator vhost to `display_host` and grant `+t`.
- `can_override` — reserved for SAJOIN, SAPART, SAMODE, SETHOST, SETIDENT and related override commands.
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
RULES
TOPIC
USER
USERHOST
USERIP
WALLOPS
WHO
WHOIS
ZLINE
```

Planned administrator/operator commands not yet implemented include:

```text
RESTART
SAJOIN
SAMODE
SAPART
SETHOST
SETIDENT
SETNAME
```

## Security and runtime data

All ScratchIRCd databases belong under `data/`. Current databases are:

```text
data/operators.db
data/bans.db
```

Future service/history databases will follow the same convention. `ircd.conf`, `data/*.db`, SQLite journal/WAL files, and `build/` are excluded from source control.
