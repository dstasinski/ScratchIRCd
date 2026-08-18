# ScratchIRCd IRC Operator Guide

This guide documents ordinary IRC operator authentication, permissions, modes, and commands. Ordinary operators are stored in `data/operators.db` and managed by the network administrator.

## Authentication

```text
OPER <operator-name> <password>
```

A successful login grants user mode `+o` and loads the permissions from the SQLite operator record. The record must be enabled. `helpop` grants `+h`; `get_host` applies the configured vhost and grants `+t`. Database operators cannot receive `+N`.

## Permission flags

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — use RESTART when implemented.
- `helpop` — receive user mode `+h`.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs.
- `get_host` — receive the configured operator vhost and `+t`.
- `can_override` — use server-authority override commands when implemented.

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

Adding requires `can_kline`; removal requires `can_unkline`. KLINE records persist in `data/bans.db`. Wildcards `*` and `?` are supported, and matching checks both effective `user@host` and `user@IP`. Matching connected clients are removed immediately and new matches are rejected before registration.

### ZLINE

```text
ZLINE <ip-mask> :<reason>
ZLINE -<ip-mask>
```

Requires `can_zline`. ZLINE matches the effective numeric client IP and persists in `data/bans.db`.

### WALLOPS

```text
WALLOPS :<message>
```

Requires `can_wallops`. Messages are delivered to registered clients using user mode `+w`.

### REHASH

```text
REHASH
```

Requires `can_rehash`. Runtime configuration is reloaded from the active `ircd.conf`. Listener address, port, server-name changes, or reducing `max_clients` below the current connection count require a restart instead and are rejected by REHASH.

## Network-administrator-only commands

Ordinary operators cannot use these database-management commands:

```text
OPERADD
OPERDEL
OPERSET
OPERLIST
```

## Planned operator commands

```text
RESTART
SAJOIN
SAMODE
SAPART
SETHOST
SETIDENT
SETNAME
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
- `+t` — using an operator vhost.
