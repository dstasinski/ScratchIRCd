# ScratchIRCd Network Administrator Guide

This guide documents commands and responsibilities available to the ScratchIRCd network administrator.

## Client identity and security policy

Every connected client has exactly three host/address identity fields:

- `real_ip` — actual end-user numeric IP address.
- `real_host` — FCrDNS-verified hostname for that actual IP, when available.
- `display_host` — the only hostname disclosed through normal IRC client-visible protocol output.

WHO, ordinary WHOIS, USERHOST, channel/user prefixes, channel ban masks, and replayable channel history use `display_host`. Vhosts (`+t`) and cloaks (`+x`) replace only `display_host`.

KLINE checks `user@real_host` when available and `user@real_ip`; ZLINE and DNSBL use only `real_ip`. GeoBAN uses only MaxMind-enriched metadata derived from final `real_ip`. Operators can inspect real identity via operator WHOIS and USERIP. Persistent chat history never stores a replayable real IP or real DNS hostname.

## Connection liveness

ScratchIRCd sends a server `PING` to a registered client after 90 seconds with
no complete inbound IRC command. The client must return the exact PING token in
a `PONG` within another 90 seconds. A missing or mismatched response disconnects
the client with `Ping Timeout: 90 seconds`; unrelated commands do not clear an
outstanding challenge.

```text
ping_interval_seconds = 90
ping_timeout_seconds = 90
```

Both values accept 1 through 3600 seconds. Changing either value requires a
RESTART so existing client deadlines cannot change underneath live sessions.

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
account-notify away-notify batch draft/chathistory extended-join labeled-response message-tags sasl=PLAIN server-time
```

This is the CAP 302 advertisement; clients request `sasl` without its value. SASL mechanism `PLAIN` authenticates against the same NickServ account records in `data/nickserv.db` and therefore shares Argon2id password validation, account state, `+r`, and NickServ vhost behavior. Registration is held while CAP negotiation is open and resumes on `CAP END`. SASL does not grant IRC operator authority; `OPER` remains separate.

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

## Persistent GeoIP policy bans

GeoBAN is separate from KLINE and ZLINE and is stored in a dedicated `geo_bans` table inside `data/bans.db`.

```text
GEOBAN <COUNTRY|REGION|ASN|ORG> <value> <duration|0> [:reason]
GEOBAN LIST
UNGEOBAN <COUNTRY|REGION|ASN|ORG> <value>
```

COUNTRY and REGION are case-insensitive and normalized uppercase. ASN accepts either `22773` or `AS22773`. ORG matches MaxMind `autonomous_system_organization` with case-insensitive Tcl-style glob matching; use braces around values containing spaces.

```text
GEOBAN COUNTRY RU 0 :Connections from this country are not accepted
GEOBAN REGION AZ 7d :Temporary regional restriction
GEOBAN ASN AS22773 1d :Network abuse
GEOBAN ORG {*Example Network*} forever :Blocked provider family
```

Durations accept `s`, `m`, `h`, `d`, and `w`. `0`, `permanent`, `perm`, and `forever` are permanent. Policies survive restart and keep their original expiration timestamp. Adding a policy immediately disconnects matching registered clients except the setter; new matching clients are rejected before registration. Operators require `can_geoban`. See `docs/GEOBAN_GUIDE.md`.

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

A successful bootstrap login receives `+N` and the complete operator permission set, including `can_die` and `can_geoban`. The bootstrap hostmask is evaluated against real identity, never `display_host` or a WebIRC gateway.

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

Registered accounts live in `data/nickserv.db`. NickServ is virtual and never joins channels or appears in NAMES, WHO, ISON, or LUSERS. See `docs/NICKSERV_GUIDE.md` for the full command set.

Network-administrator account commands are:

```text
NSINFO <account>
NSSET <account> PASSWORD <new-password>
NSSET <account> VHOST <vhost|->
NSSET <account> EMAIL <address|->
NSSET <account> ENABLED <0|1>
NSDROP <account>
```

## ChanServ registered-channel management

Registered channels live in `data/chanserv.db`. ChanServ is virtual and has server authority without joining channels. See `docs/CHANSERV_GUIDE.md` for the full command set.

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

MemoServ stores account-to-account messages in `data/memoserv.db`. See `docs/MEMOSERV_GUIDE.md` for user commands.

Network-administrator MemoServ commands are:

```text
MSINFO <account>
MSPURGE <account|*>
```

## Persistent server bans

### KLINE

```text
KLINE <nickname>
KLINE <user@host-mask> :<reason>
KLINE -<user@host-mask>
```

Explicit KLINE persists in `data/bans.db`, matches real host/IP identity, and ignores cloaks/vhosts. Nickname shorthand creates a temporary policy using the configured default duration and reason.

### ZLINE

```text
ZLINE <nickname>
ZLINE <ip-mask> :<reason>
ZLINE -<ip-mask>
```

Explicit ZLINE persists in `data/bans.db` and matches only `real_ip`. Nickname shorthand creates a temporary exact-IP policy using the configured default duration and reason. For WebIRC users this is the end-user IP, not the gateway IP.

### GeoBAN

GeoBAN policies are stored separately from KLINE/ZLINE in `geo_bans` within the same `data/bans.db`. They match COUNTRY, REGION, ASN, or ORG metadata only.

## Operator-controlled moderation

```text
DEAF +<nick>
DEAF -<nick>
MUTE +<nick>
MUTE -<nick>
```

`DEAF` controls user mode `+D`. `MUTE` controls user mode `+M`; +M affects only ordinary channel members, while +v/+h/+o/+a/+q members are immune in that channel and IRCops/network administrators are globally immune. See `docs/MODERATION_GUIDE.md`.

## Graceful shutdown and restart

```text
DIE
RESTART
```

`DIE` requires `can_die` and requests a clean daemon shutdown. It exits the event loop, disconnects remaining clients through the normal server teardown path, closes listeners and worker resources, and then terminates the process. `RESTART` requires `can_restart` and uses the same teardown path before rebuilding the server in the same process.

## Operator permissions

- `can_rehash` — use REHASH.
- `can_die` — use DIE for graceful daemon shutdown.
- `can_restart` — use RESTART.
- `helpop` — grants `+h` on OPER login.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs.
- `can_geoban` — add/list/remove COUNTRY/REGION/ASN/ORG GeoBAN policies.
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
DEAF
DIE
FLASH
GEOBAN
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
MSINFO
MSPURGE
MUTE
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
SILENCE
TOPIC
UNGEOBAN
USER
USERHOST
USERIP
WALLOPS
WATCH
WEBIRC
WHO
WHOIS
WHOWAS
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
