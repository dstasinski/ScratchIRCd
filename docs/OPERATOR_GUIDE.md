# ScratchIRCd IRC Operator Guide

This guide documents ordinary IRC operator authentication, permissions, modes, and commands. Ordinary operators are stored in `data/operators.db` and managed by the network administrator.

## Client host identity

ScratchIRCd keeps three host/address values for each client:

- `real_ip` — actual end-user numeric IP.
- `real_host` — FCrDNS-verified hostname for the actual IP, when available.
- `display_host` — the public hostname shown to ordinary IRC users.

WHO, ordinary WHOIS, USERHOST, channel traffic, and channel bans use `display_host`. A vhost (`+t`) changes only `display_host`; future cloaking (`+x`) will do the same. KLINE, ZLINE, DNSBL, and GeoIP ignore the displayed hostname and use the real security identity.

For authenticated WebIRC users, `real_ip` and `real_host` describe the actual end user, never the gateway. Gateway audit metadata is kept separately. Successful WebIRC users are marked `+V`.

IRC operators may inspect real identity through operator WHOIS numeric 378 and USERIP. Ordinary users are denied USERIP.

## NickServ account state and SASL

NickServ is a virtual service, not a Client. It never joins channels and does not appear in NAMES, WHO, ISON, or LUSERS. A successful NickServ IDENTIFY or SASL login stores an account name on the Client and sets service-controlled user mode `+r`.

Operators may authenticate their personal NickServ account before registration with IRCv3 SASL PLAIN:

```text
CAP LS 302
CAP REQ :sasl
AUTHENTICATE PLAIN
AUTHENTICATE <base64-data>
CAP END
```

SASL uses the same NickServ password hash and vhost path as IDENTIFY. It does not confer IRC operator privileges; `OPER` remains a separate authorization step.

Operators have the same personal account-management commands as ordinary users:

```text
NICKSERV REGISTER <password>
NICKSERV IDENTIFY [account] <password>
NICKSERV RECOVER <nick>
NICKSERV RECOVER <nick> KILL
NICKSERV GHOST <nick>
NICKSERV SET PASSWORD <new-password>
NICKSERV SET EMAIL <address>
NICKSERV VERIFY <token>
NICKSERV RESET <account>
NICKSERV RESET <account> <token> <new-password>
NICKSERV HELP
IDENTIFY <password>
IDENTIFY <account> <password>
```

`RECOVER` and `GHOST` authorize against the authenticated NickServ account, not IRC-operator privileges. Default RECOVER renames the occupying session; `RECOVER ... KILL` and `GHOST` disconnect it. These actions therefore do not require `can_kill`.

A NickServ vhost is applied only to `display_host` and sets `+t`; it never changes `real_ip` or `real_host`. User mode `+r` cannot be manufactured with MODE or SAMODE. See `docs/NICKSERV_GUIDE.md` for full service behavior and email-reset details.

## Operator authentication

```text
OPER <operator-name> <password>
```

Successful login grants `+o` and loads permissions from the SQLite operator record. `helpop` grants `+h`; `get_host` applies the configured operator vhost to `display_host` and grants `+t`. Database operators cannot receive `+N`.

## Permission flags

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — use RESTART.
- `helpop` — receive `+h`.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs, including automatic DNSBL-generated ZLINEs.
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

Configured DNS blacklists automatically create exact-IP ZLINE records in `data/bans.db`. Their reason identifies the DNSBL name and zone, and `set_by` begins with `DNSBL:`. These bans behave exactly like manually created ZLINEs. An operator with `can_zline` can remove one with:

```text
ZLINE -203.0.113.42
```

REHASH reloads safely mutable runtime configuration, including WebIRC gateway authorization, DNSBL definitions/timeouts, database paths, and NickServ mail settings. Listener/TLS changes require RESTART. SAJOIN/SAPART/SAMODE/SETHOST/SETIDENT/SETNAME require `can_override`.

User SAMODE cannot manufacture provenance/security modes such as `+N`, `+o`, `+r`, `+S`, `+t`, `+V`, `+x`, or `+z`.

## Network-administrator-only commands

Ordinary operators cannot manage operator or NickServ account records directly:

```text
OPERADD
OPERDEL
OPERSET
OPERLIST
NSINFO
NSSET
NSDROP
```

## General commands available to operators

```text
ADMIN
AUTHENTICATE
AWAY
CAP
IDENTIFY
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
NICKSERV
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
- `+r` — authenticated NickServ account; service-controlled.
- `+s` — server-notice reception; full behavior still planned.
- `+w` — receive WALLOPS.
- `+W` — WHOIS notification for IRCops; full behavior still planned.
- `+t` — using an operator/NickServ vhost; changes `display_host` only.
- `+V` — authenticated WebIRC end user.
- `+x` — cloaked displayed hostname; cloak generation is still planned.
- `+z` — authenticated TLS transport.
