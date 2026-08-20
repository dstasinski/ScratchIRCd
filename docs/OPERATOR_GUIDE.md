# ScratchIRCd IRC Operator Guide

This guide documents ordinary IRC operator authentication, permissions, modes, and commands. Ordinary operators are stored in `data/operators.db` and managed by the network administrator.

## Client host identity

ScratchIRCd keeps three host/address values for each client:

- `real_ip` — actual end-user numeric IP.
- `real_host` — FCrDNS-verified hostname for the actual IP, when available.
- `display_host` — the public hostname shown to ordinary IRC users.

WHO, ordinary WHOIS, USERHOST, channel traffic, and channel bans use `display_host`. A vhost (`+t`) changes only `display_host`; future cloaking (`+x`) will do the same. KLINE and ZLINE ignore the displayed hostname and use the real security identity.

For authenticated WebIRC users, `real_ip` and `real_host` describe the actual end user, never the gateway. Gateway audit metadata is kept separately. Successful WebIRC users are marked `+V`.

IRC operators may inspect real identity through operator WHOIS numeric 378 and USERIP. Ordinary users are denied USERIP.

## Authentication

```text
OPER <operator-name> <password>
```

Successful login grants `+o` and loads permissions from the SQLite operator record. `helpop` grants `+h`; `get_host` applies the configured vhost to `display_host` and grants `+t`. Database operators cannot receive `+N`.

## Permission flags

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — use RESTART.
- `helpop` — receive `+h`.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs.
- `get_host` — receive the configured operator vhost and `+t`.
- `can_override` — use SAJOIN, SAPART, SAMODE, SETHOST, SETIDENT, and SETNAME.

`netadmin` is reserved for the bootstrap network administrator.

## Implemented operator commands

```text
KILL <nickname> :<reason>
KLINE <user@host-mask> :<reason>
KLINE -<user@host-mask>
ZLINE <ip-mask> :<reason>
ZLINE -<ip-mask>
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

KLINE matches `user@real_host` and `user@real_ip`; ZLINE matches only `real_ip`. Thus a WebIRC user's bans apply to the actual end user rather than the gateway. SETHOST changes only `display_host` and never changes real identity. USERIP and operator WHOIS reveal the real identity.

REHASH reloads safely mutable runtime configuration, including WebIRC gateway authorization. Listener/TLS changes require RESTART. SAJOIN/SAPART/SAMODE/SETHOST/SETIDENT/SETNAME require `can_override`.

User SAMODE cannot manufacture provenance/security modes such as `+N`, `+o`, `+r`, `+S`, `+t`, `+V`, `+x`, or `+z`.

## Network-administrator-only commands

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

`WEBIRC` is implemented but is a pre-registration gateway command rather than an ordinary operator command.

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
- `+V` — authenticated WebIRC end user.
- `+x` — cloaked displayed hostname; cloak generation is still planned.
- `+z` — authenticated TLS transport.
