# ScratchIRCd IRC Operator Guide

This guide documents ordinary IRC operator authentication, permissions, modes, and commands. Ordinary operators are stored in `data/operators.db` and managed by the network administrator.

## Client host identity

ScratchIRCd keeps three host/address values for each client:

- `real_ip` — actual end-user numeric IP.
- `real_host` — FCrDNS-verified hostname for the actual IP, when available.
- `display_host` — the public hostname shown to ordinary IRC users.

WHO, ordinary WHOIS, USERHOST, channel traffic, and channel bans use `display_host`. A vhost (`+t`) changes only `display_host`; future cloaking (`+x`) will do the same. KLINE and ZLINE ignore the displayed hostname and use the real security identity.

IRC operators may inspect the real identity through operator WHOIS output (numeric 378) and USERIP. Ordinary users are denied USERIP.

For future WebIRC clients, `real_ip` and `real_host` will describe the actual end user rather than the WebIRC gateway.

## Authentication

```text
OPER <operator-name> <password>
```

A successful login grants user mode `+o` and loads the permissions from the SQLite operator record. The record must be enabled. `helpop` grants `+h`; `get_host` applies the configured vhost to `display_host` and grants `+t`. Database operators cannot receive `+N`.

## Permission flags

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — use RESTART.
- `helpop` — receive user mode `+h`.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs.
- `get_host` — receive the configured operator vhost and `+t`.
- `can_override` — use SAJOIN, SAPART, SAMODE, SETHOST, SETIDENT, and SETNAME.

`netadmin` is reserved for the bootstrap network administrator and cannot be assigned to ordinary operators.

## Implemented operator commands

### KILL

```text
KILL <nickname> :<reason>
```

Requires `can_kill`. Services-protected clients cannot be killed through ordinary KILL. Ordinary operators also cannot KILL a network administrator.

### KLINE

```text
KLINE <user@host-mask> :<reason>
KLINE -<user@host-mask>
```

Adding requires `can_kline`; removal requires `can_unkline`. KLINE records persist in `data/bans.db`. Wildcards `*` and `?` are supported. Matching checks `user@real_host` when a verified hostname exists and also `user@real_ip`. `display_host`, including cloaks and vhosts, is never used for KLINE.

### ZLINE

```text
ZLINE <ip-mask> :<reason>
ZLINE -<ip-mask>
```

Requires `can_zline`. ZLINE matches only `real_ip` and persists in `data/bans.db`.

### WALLOPS

```text
WALLOPS :<message>
```

Requires `can_wallops`. Messages are delivered to registered clients using user mode `+w`.

### REHASH

```text
REHASH
```

Requires `can_rehash`. Runtime configuration is reloaded from the active `ircd.conf`. Listener address, port, server-name changes, or reducing `max_clients` below the current connection count require a restart instead.

### RESTART

```text
RESTART
```

Requires `can_restart`. ScratchIRCd disconnects current clients, destroys the active Server instance, reloads the current configuration file, recreates listeners/databases, and starts a fresh Server instance in the same process.

### SAJOIN

```text
SAJOIN <nick> <channel>[,<channel>...]
```

Requires `can_override`. Forces the target client into the requested channels without applying normal JOIN restrictions such as keys, bans, invite-only, limits, TLS-only, or account-only rules.

### SAPART

```text
SAPART <nick> <channel>[,<channel>...]
```

Requires `can_override`. Forces the target client to leave the listed channels.

### SAMODE

```text
SAMODE <nick> <modes>
SAMODE <channel> <modes> [parameters...]
```

Requires `can_override`. Channel SAMODE uses the normal MODE parser with server authority, bypassing channel-ownership requirements. User SAMODE may force ordinary behavioral modes but cannot manufacture provenance/security modes such as network-admin, oper, registered-account, service, vhost, WebIRC, cloak, or TLS state.

### SETHOST

```text
SETHOST <nick> <newhost>
```

Requires `can_override`. Changes only the target's `display_host`, sets `+t`, and clears `+x`. It never changes `real_ip` or `real_host`.

### SETIDENT

```text
SETIDENT <nick> <newident>
```

Requires `can_override`. Changes the target's IRC ident/user field.

### SETNAME

```text
SETNAME <nick> :<new real name>
```

Requires `can_override`. Changes the target's real-name/gecos field.

### USERIP

```text
USERIP <nick1> [nick2 ...]
```

Requires IRC operator status and returns each target's `real_ip`.

### WHOIS real identity

Operators receive numeric 378 containing the target's `real_host` (or real IP when no verified hostname exists) and `real_ip`. Normal clients receive only `display_host` through the standard WHOIS user reply.

## Network-administrator-only commands

Ordinary operators cannot use these database-management commands:

```text
OPERADD
OPERDEL
OPERSET
OPERLIST
```

## General commands available to operators

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

## Operator-related user modes

- `+o` — IRC operator.
- `+N` — network administrator; bootstrap administrator only.
- `+h` — HelpOp.
- `+H` — hide IRCop status; full behavior still planned.
- `+I` — hide operator idle time from regular users.
- `+g` — globops/locops capability; full behavior still planned.
- `+s` — server-notice reception; full behavior still planned.
- `+w` — receive WALLOPS.
- `+W` — WHOIS notification for IRCops; full behavior still planned.
- `+t` — using an operator/NickServ vhost; changes `display_host` only.
- `+x` — cloaked displayed hostname; cloak-generation behavior is still planned.
