# ScratchIRCd IRC Operator Guide

This guide documents ordinary IRC operator authentication, permissions, modes, and commands. Ordinary operators are stored in `data/operators.db` and managed by the network administrator.

## Client host identity

ScratchIRCd keeps three host/address values for each client:

- `real_ip` — actual end-user numeric IP.
- `real_host` — FCrDNS-verified hostname for the actual IP, when available.
- `display_host` — the public hostname shown to ordinary IRC users.

WHO, ordinary WHOIS, USERHOST, channel traffic, channel bans, and replayable channel history use `display_host`. A vhost (`+t`) changes only `display_host`; future cloaking (`+x`) will do the same. KLINE, ZLINE, DNSBL, and GeoIP ignore the displayed hostname and use the real security identity.

For authenticated WebIRC users, `real_ip` and `real_host` describe the actual end user, never the gateway. Gateway audit metadata is kept separately. Successful WebIRC users are marked `+V`.

IRC operators may inspect real identity through operator WHOIS numeric 378 and USERIP. Ordinary users are denied USERIP. Persistent chat history never exposes the real IP or real DNS hostname.

## IRCv3 history

Operators may use the same IRCv3 channel-history interface as ordinary clients. Negotiate:

```text
CAP REQ :batch draft/chathistory server-time
```

then, while a member of the target channel:

```text
CHATHISTORY LATEST <channel> * <limit>
```

History is stored in `data/history.db` by default. Operator status does not bypass the current membership requirement for CHATHISTORY; SAJOIN may be used separately when the operator has `can_override` and needs server-authority channel entry.

## NickServ account state and SASL

NickServ is a virtual service, not a Client. It never joins channels and does not appear in NAMES, WHO, ISON, or LUSERS. A successful NickServ IDENTIFY or SASL login stores an account name on the Client and sets service-controlled user mode `+r`.

Operators may authenticate their personal NickServ account before registration with IRCv3 SASL PLAIN. SASL uses the same NickServ password hash and vhost path as IDENTIFY and does not confer IRC operator privileges; `OPER` remains separate.

Operators have the same personal NickServ account-management commands as ordinary users. See `docs/NICKSERV_GUIDE.md` for the complete command set.

## ChanServ registered channels

ChanServ is a virtual service and never joins channels. Ordinary IRC operator status does not itself confer ChanServ founder authority. Operators authenticated to a founder/access account may use the normal ChanServ commands and receive the corresponding account-based OWNER/PROTECTED/OP/HALFOP/VOICE privileges. Network-administrator-only `CSINFO`, `CSSET`, and `CSDROP` remain separate. See `docs/CHANSERV_GUIDE.md`.

## MemoServ

MemoServ is also virtual and account-based. Operators may use the same personal MemoServ commands as ordinary clients:

```text
MEMOSERV SEND <account> :<message>
MEMOSERV LIST
MEMOSERV SENT
MEMOSERV READ <memo-id>
MEMOSERV REPLY <memo-id> :<message>
MEMOSERV FORWARD <memo-id> <account>
MEMOSERV DEL <memo-id|ALL>
MEMOSERV STATUS
MEMOSERV HELP
```

MemoServ authority is based on the authenticated NickServ account, not IRC operator status. Ordinary operators do not gain access to other accounts' memo contents. `MSINFO` and `MSPURGE` are network-administrator-only. See `docs/MEMOSERV_GUIDE.md`.

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

Configured DNS blacklists automatically create exact-IP ZLINE records in `data/bans.db`. An operator with `can_zline` can remove one with `ZLINE -<ip>`.

REHASH reloads safely mutable runtime configuration, including WebIRC gateways, DNSBL definitions/timeouts, database paths, NickServ mail settings, history settings, and MemoServ quota/retention settings. Listener/TLS changes require RESTART. SAJOIN/SAPART/SAMODE/SETHOST/SETIDENT/SETNAME require `can_override`.

User SAMODE cannot manufacture provenance/security modes such as `+N`, `+o`, `+r`, `+S`, `+t`, `+V`, `+x`, or `+z`.

## Network-administrator-only commands

Ordinary operators cannot directly manage operator, NickServ, ChanServ, or MemoServ administrative state:

```text
OPERADD
OPERDEL
OPERSET
OPERLIST
NSINFO
NSSET
NSDROP
CSINFO
CSSET
CSDROP
MSINFO
MSPURGE
```

## General commands available to operators

```text
ADMIN
AUTHENTICATE
AWAY
CAP
CHANSERV
CHATHISTORY
IDENTIFY
INVITE
ISON
JOIN
KICK
KILL
KLINE
LIST
LUSERS
MEMOSERV
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
