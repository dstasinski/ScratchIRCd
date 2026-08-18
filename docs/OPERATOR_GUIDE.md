# ScratchIRCd IRC Operator Guide

This guide documents ordinary IRC operator authentication, permissions, modes, and commands. It will be updated as operator commands are implemented.

## Operator accounts

Ordinary IRC operators are stored in the SQLite `operators.db` database and are managed by the network administrator. They are not configured in `ircd.conf`.

Authenticate with:

```text
OPER <operator-name> <password>
```

A successful login grants user mode `+o` and loads the permission set stored in the operator record. An operator record must be enabled.

If the record contains `helpop`, `+h` is also granted. If it contains `get_host` and a vhost is configured, that vhost is applied and user mode `+t` is granted. Database operators cannot receive network-administrator mode `+N`.

## Permission flags

The database `permissions` field is a comma-separated list. Defined permissions are:

- `can_rehash` — use REHASH when implemented.
- `can_die` — use DIE when implemented.
- `can_restart` — use RESTART when implemented.
- `helpop` — receive user mode `+h`.
- `can_wallops` — send WALLOPS when implemented.
- `can_kill` — use KILL when implemented.
- `can_kline` — add KLINEs when implemented.
- `can_unkline` — remove KLINEs when implemented.
- `can_zline` — use ZLINE when implemented.
- `get_host` — receive the configured operator vhost and `+t`.
- `can_override` — use server-authority override commands when implemented.

`netadmin` is not a valid ordinary-operator permission. It is reserved for the bootstrap network administrator.

## Operator commands currently implemented

At this stage, `OPER` is the implemented operator-specific command:

```text
OPER <operator-name> <password>
```

The following requested operator commands are planned and will enforce their corresponding permission bits:

```text
KILL
KLINE
REHASH
RESTART
SAJOIN
SAMODE
SAPART
SETHOST
SETIDENT
SETNAME
WALLOPS
ZLINE
```

Operator database management commands (`OPERADD`, `OPERDEL`, `OPERSET`, `OPERLIST`) are network-administrator-only and are not available to ordinary operators.

## General client commands

Operators may also use the normal implemented IRC commands:

```text
ADMIN
AWAY
INVITE
ISON
JOIN
KICK
LIST
LUSERS
MODE
MOTD
NAMES
NICK
NOTICE
PART
PASS
PING
PONG
PRIVMSG
QUIT
RULES
TOPIC
USER
USERHOST
USERIP
WHO
WHOIS
```

## Operator-related user modes

Relevant user modes include:

- `+o` — IRC operator.
- `+N` — network administrator; bootstrap administrator only.
- `+h` — HelpOp.
- `+H` — hide IRCop status; reserved for operator behavior as implementation expands.
- `+I` — hide an operator's idle time from regular users.
- `+g` — globops/locops capability as implementation expands.
- `+s` — server notices as implementation expands.
- `+w` — wallops reception as implementation expands.
- `+W` — WHOIS notification for IRCops as implementation expands.
- `+t` — indicates an applied vhost.

A mode being represented internally does not necessarily mean every associated behavior is implemented yet. This guide distinguishes implemented commands from planned ones accordingly.
