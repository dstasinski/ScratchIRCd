# ScratchIRCd IRC Operator Guide

This guide documents ordinary IRC operator authentication, permissions, modes, and commands. Ordinary operators are stored in `data/operators.db` and managed by the network administrator.

## Client host identity

ScratchIRCd keeps three host/address values for each client:

- `real_ip` — actual end-user numeric IP.
- `real_host` — FCrDNS-verified hostname for the actual IP, when available.
- `display_host` — the public hostname shown to ordinary IRC users.

WHO, ordinary WHOIS, USERHOST, channel traffic, channel bans, and replayable channel history use `display_host`. A vhost (`+t`) or cloak (`+x`) changes only `display_host`. KLINE, ZLINE, DNSBL, and GeoIP ignore the displayed hostname and use the real security identity.

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

## Operator message and notice modes

`+g` and `+s` are operator-only self-toggleable modes:

```text
MODE <nick> +g
MODE <nick> +s
```

`+g` enables receipt and sending of operator-message traffic:

```text
GLOBOPS :<message>
LOCOPS :<message>
```

Sending either command requires both IRC operator status and `+g`. ScratchIRCd is intentionally single-server, so both are local-process broadcasts even though the command names remain distinct.

`+s` enables daemon-generated server notices. Current notices include meaningful security/administrative events such as KILL, KLINE, and ZLINE changes.

## Implemented operator commands

```text
GLOBOPS :<message>
KILL <nickname> :<reason>
KLINE <nickname>
KLINE <user@host-mask> :<reason>
KLINE -<user@host-mask>
LOCOPS :<message>
ZLINE <nickname>
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

Explicit KLINE matches `user@real_host` and `user@real_ip`; explicit ZLINE matches only `real_ip`. Thus a WebIRC user's bans apply to the actual end user rather than the gateway. SETHOST changes only `display_host` and never changes real identity. USERIP and operator WHOIS reveal the real identity.

`KLINE <nickname>` is a shorthand temporary ban. ScratchIRCd resolves the live target to `*@real_host`, falling back to `*@real_ip` when no verified hostname is available. `ZLINE <nickname>` resolves to the target's exact `real_ip`. Both shorthand forms use the configured `*_default_duration_seconds` and `*_default_reason` settings. Explicit mask commands remain permanent. Expired temporary records are ignored and automatically purged from `data/bans.db`.

Configured DNS blacklists automatically create exact-IP ZLINE records in `data/bans.db`. An operator with `can_zline` can remove one with `ZLINE -<ip>`.

REHASH reloads safely mutable runtime configuration, including WebIRC gateways, DNSBL definitions/timeouts, database paths, NickServ mail settings, history settings, MemoServ quota/retention settings, and nickname KLINE/ZLINE default duration/reason values. Listener/TLS changes require RESTART. SAJOIN/SAPART/SAMODE/SETHOST/SETIDENT/SETNAME require `can_override`.

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
GLOBOPS
IDENTIFY
INVITE
ISON
JOIN
KICK
KILL
KLINE
KNOCK
LIST
LOCOPS
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
SILENCE
TOPIC
USER
USERHOST
USERIP
WALLOPS
WATCH
WHO
WHOIS
WHOWAS
ZLINE
```

`WEBIRC` is implemented but is a pre-registration gateway command rather than an ordinary operator command.

## Operator-related user modes

- `+o` — IRC operator.
- `+N` — network administrator; bootstrap administrator only.
- `+h` — HelpOp.
- `+H` — hide IRCop status from regular users; IRCop-self-toggleable.
- `+I` — hide operator idle time from regular users; IRCop-self-toggleable.
- `+g` — receive/send GLOBOPS and LOCOPS; IRCop-self-toggleable.
- `+r` — authenticated NickServ account; service-controlled.
- `+s` — receive daemon-generated server notices; IRCop-self-toggleable.
- `+w` — receive WALLOPS.
- `+W` — receive WHOIS notifications; IRCop-self-toggleable.
- `+t` — using an operator/NickServ vhost; changes `display_host` only.
- `+V` — authenticated WebIRC end user.
- `+x` — cloaked displayed hostname; changes `display_host` only.
- `+z` — authenticated TLS transport.
