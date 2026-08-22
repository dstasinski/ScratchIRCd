# ScratchIRCd Network Administrator Guide

This guide documents commands and responsibilities available to the ScratchIRCd network administrator.

## Client identity and security policy

Every connected client has exactly three host/address identity fields:

- `real_ip` — actual end-user numeric IP address.
- `real_host` — FCrDNS-verified hostname for that actual IP, when available.
- `display_host` — the only hostname disclosed through normal IRC client-visible protocol output.

WHO, ordinary WHOIS, USERHOST, channel/user prefixes, channel ban masks, and replayable channel history use `display_host`. Vhosts (`+t`) replace only `display_host`; planned cloaking (`+x`) will do the same.

KLINE checks `user@real_host` when available and `user@real_ip`; ZLINE and DNSBL use only `real_ip`. Operators can inspect real identity via operator WHOIS and USERIP. Persistent chat history never stores a replayable real IP or real DNS hostname.

## TLS configuration

```text
port = 6667
tls_port = 6697
tls_cert_file = /etc/letsencrypt/live/irc.example.net/fullchain.pem
tls_key_file = /etc/letsencrypt/live/irc.example.net/privkey.pem
```

TLS is enabled only when both certificate and private-key paths are configured. TLS 1.2 is the minimum accepted protocol version. Successful TLS clients receive `+z`. TLS listener/certificate changes require RESTART.

## IRCv3 capabilities, SASL, and history

ScratchIRCd currently advertises:

```text
account-notify batch draft/chathistory sasl server-time
```

SASL mechanism `PLAIN` authenticates against the same NickServ account records in `data/nickserv.db` and therefore shares Argon2id password validation, account state, `+r`, and NickServ vhost behavior. Registration is held while CAP negotiation is open and resumes on `CAP END`. SASL does not grant IRC operator authority; `OPER` remains separate.

Accepted channel PRIVMSG/NOTICE history is stored in SQLite:

```text
history_db = data/history.db
history_limit = 100
```

The requester must currently be a channel member to use `CHATHISTORY LATEST`. The database persists across restarts and replays only the public displayed identity captured at send time. ScratchIRCd advertises `MSGREFTYPES=timestamp` and `CHATHISTORY=<limit>` in numeric 005.

## WebIRC gateways

Authorized gateways are configured by numeric TCP peer IP and password:

```text
webirc_gateway = 127.0.0.1 gateway-secret
webirc_gateway = 2001:db8::10 another-secret
```

The gateway sends before registration:

```text
WEBIRC <password> <gateway-name> <supplied-hostname> <client-ip>
```

Authorization requires both the configured numeric gateway IP and matching password. Gateway DNS names are not used for authorization. The supplied end-user IP becomes `real_ip`; gateway audit data remains separate.

## DNS blacklist enforcement

```text
dnsbl_timeout_seconds = 5
dnsbl = Spamhaus zen.spamhaus.org
dnsbl = DroneBL dnsbl.dronebl.org
```

DNSBL checks run asynchronously after final direct/WebIRC `real_ip` is established. A positive result creates an exact-IP persistent ZLINE in `data/bans.db`. Timeouts and resolver failures fail open. Authorized operators can remove automatic ZLINEs with `ZLINE -<ip>`.

## MaxMind GeoIP / ASN

```text
geoip_city_db = data/GeoLite2-City.mmdb
geoip_asn_db = data/GeoLite2-ASN.mmdb
```

The files are optional. Lookup uses final `real_ip`. Client GeoIP state includes status, IP/network/source, continent/country/region/city data, ASN, and organization. Changing MMDB paths requires RESTART.

## Bootstrap network administrator

Only the bootstrap network administrator is stored in `ircd.conf`. Ordinary IRC operators live in `data/operators.db`.

```text
operators_db = data/operators.db
bans_db = data/bans.db
nickserv_db = data/nickserv.db
chanserv_db = data/chanserv.db
memoserv_db = data/memoserv.db
history_db = data/history.db
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

## NickServ account management

Registered accounts live in `data/nickserv.db`. NickServ is virtual and never joins channels or appears in NAMES, WHO, ISON, or LUSERS.

User/account-owner commands are:

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
```

Network-administrator account commands are:

```text
NSINFO <account>
NSSET <account> PASSWORD <new-password>
NSSET <account> VHOST <vhost|->
NSSET <account> EMAIL <address|->
NSSET <account> ENABLED <0|1>
NSDROP <account>
```

`NSINFO` never exposes password or token hashes. Optional email verification/recovery uses the configured local sendmail-compatible MTA. See `docs/NICKSERV_GUIDE.md`.

## ChanServ registered-channel management

Registered channels live in:

```text
chanserv_db = data/chanserv.db
```

ChanServ is virtual and has server authority without joining channels. Users can address it as `CHANSERV ...` or `PRIVMSG ChanServ :...`.

Account-owner commands include registration, information, access management, persistent settings, and drop. Founder/access roles are account-based and include OWNER, PROTECTED, OP, HALFOP, and VOICE. Persistent channel state includes MLOCK, topic, parameter modes, and `+b/+e/+I` lists. See `docs/CHANSERV_GUIDE.md` for the complete command set.

Network-administrator commands are:

```text
CSINFO <#channel>
CSSET <#channel> DESCRIPTION <text>
CSSET <#channel> FOUNDER <NickServ-account>
CSSET <#channel> ENABLED <0|1>
CSDROP <#channel>
```

Numeric 005 includes the ScratchIRCd extension `PCHANNELS=` listing enabled ChanServ registrations.

## MemoServ administration

MemoServ stores account-to-account messages in:

```text
memoserv_db = data/memoserv.db
memoserv_quota = 100
memoserv_retention_days = 90
```

`memoserv_quota` limits the number of stored inbox memos per account. `memoserv_retention_days` expires messages by creation time; `0` disables automatic expiration. User MemoServ commands are `SEND`, `LIST`, `SENT`, `READ`, `REPLY`, `FORWARD`, `DEL`, `STATUS`, and `HELP`.

Network-administrator MemoServ commands are:

```text
MSINFO <account>
MSPURGE <account|*>
```

`MSINFO` reports stored and unread counts plus the active quota/retention policy without displaying memo contents. `MSPURGE` removes messages older than the configured retention period for one account or globally with `*`.

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

ZLINE persists in `data/bans.db` and matches only `real_ip`. For WebIRC users this is the end-user IP, not the gateway IP.

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

`SETHOST` changes only `display_host`; `USERIP` and operator WHOIS reveal real identity. REHASH reloads safely mutable configuration, including service database paths, WebIRC/DNSBL definitions, NickServ mail settings, history settings, and MemoServ quota/retention settings. Listener/TLS and GeoIP path changes require RESTART.

## Operator permissions

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — use RESTART.
- `helpop` — grants `+h` on OPER login.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs.
- `get_host` — apply configured operator vhost and grant `+t`.
- `can_override` — use SAJOIN, SAPART, SAMODE, SETHOST, SETIDENT, and SETNAME.
- `netadmin` — bootstrap network administrator only.

## Complete implemented command set available to the network administrator

```text
ADMIN
AUTHENTICATE
AWAY
CAP
CHANSERV
CHATHISTORY
CSDROP
CSINFO
CSSET
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
MSINFO
MSPURGE
NAMES
NICK
NICKSERV
NOTICE
NSDROP
NSINFO
NSSET
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

The network administrator may also use every NickServ, ChanServ, and MemoServ subcommand documented in their respective service guides.

## Security and runtime data

Runtime data belongs under `data/`:

```text
data/operators.db
data/bans.db
data/nickserv.db
data/chanserv.db
data/memoserv.db
data/history.db
data/GeoLite2-City.mmdb
data/GeoLite2-ASN.mmdb
```

`ircd.conf`, runtime databases, SQLite journal/WAL files, downloaded MMDB files, and `build/` are excluded from source control.
