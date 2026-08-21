# ScratchIRCd Network Administrator Guide

This guide documents commands and responsibilities available to the ScratchIRCd network administrator.

## Client identity and security policy

Every connected client has exactly three host/address identity fields:

- `real_ip` — actual end-user numeric IP address.
- `real_host` — FCrDNS-verified hostname for that actual IP, when available.
- `display_host` — the only hostname disclosed through normal IRC client-visible protocol output.

WHO, ordinary WHOIS, USERHOST, channel/user prefixes, and channel ban masks use `display_host`. Vhosts (`+t`) replace only `display_host`; planned cloaking (`+x`) will do the same.

KLINE checks `user@real_host` when available and `user@real_ip`; ZLINE and DNSBL use only `real_ip`. Operators can inspect real identity via operator WHOIS and USERIP.

## TLS configuration

```text
port = 6667
tls_port = 6697
tls_cert_file = /etc/letsencrypt/live/irc.example.net/fullchain.pem
tls_key_file = /etc/letsencrypt/live/irc.example.net/privkey.pem
```

TLS is enabled only when both certificate and private-key paths are configured. TLS 1.2 is the minimum accepted protocol version. Successful TLS clients receive `+z`. TLS listener/certificate changes require RESTART.

## WebIRC gateways

Authorized gateways are configured in `ircd.conf` by numeric TCP peer IP and password. Repeat the setting to authorize multiple gateways:

```text
webirc_gateway = 127.0.0.1 gateway-secret
webirc_gateway = 2001:db8::10 another-secret
```

The gateway sends, before registration:

```text
WEBIRC <password> <gateway-name> <supplied-hostname> <client-ip>
```

Authorization requires both the configured numeric gateway IP and matching password. Gateway DNS names are not used for authorization. After successful WEBIRC, gateway information is saved only in `Client.webirc` audit metadata, `real_ip` becomes the supplied end-user IP, asynchronous FCrDNS restarts for that address, `display_host` follows the actual client identity, and user mode `+V` is set.

A failed/unauthorized WEBIRC attempt disconnects before registration. Protect `ircd.conf` with suitable filesystem permissions and prefer TLS between remote gateways and ScratchIRCd.

## DNS blacklist enforcement

DNSBL checks are configured with repeatable entries. ScratchIRCd currently supports up to eight configured lists:

```text
dnsbl_timeout_seconds = 5
dnsbl = Spamhaus zen.spamhaus.org
dnsbl = DroneBL dnsbl.dronebl.org
```

The first field is the descriptive name written into ban metadata; the second is the DNS blacklist zone. The check runs only after the final direct/WebIRC `real_ip` is established. All DNSBL resolver calls run on a worker thread, so slow external DNS does not block the IRC event loop.

A positive result writes an exact-IP ZLINE to `data/bans.db` and rejects the client. Timeout/submission failure fails open. DNSBL-generated ZLINEs are ordinary ZLINE records and may be removed with `ZLINE -<ip>` by an authorized operator. DNSBL configuration changes may be applied with REHASH.

## MaxMind GeoIP / ASN

ScratchIRCd uses libmaxminddb directly:

```text
geoip_city_db = data/GeoLite2-City.mmdb
geoip_asn_db = data/GeoLite2-ASN.mmdb
```

The files are optional. GeoIP lookup occurs immediately before registration, after the final direct/WebIRC `real_ip` and FCrDNS state are established. Each Client contains `geoip.status`, `geoip.ip`, `geoip.network`, `geoip.source`, `geoip.continent_code`, `geoip.country_code`, `geoip.country_name`, `geoip.region_code`, `geoip.region_name`, `geoip.city`, `geoip.asn`, and `geoip.organization`.

Changing either GeoIP database path requires RESTART rather than REHASH.

## Bootstrap network administrator

Only the bootstrap network administrator is stored in `ircd.conf`. Ordinary IRC operators live in `data/operators.db`.

```text
operators_db = data/operators.db
bans_db = data/bans.db
nickserv_db = data/nickserv.db
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

Registered nickname/account records are stored in `data/nickserv.db`. Users normally create their own account by messaging the virtual NickServ service:

```text
PRIVMSG NickServ :REGISTER <password>
```

NickServ is not a real Client. It never joins channels or appears in NAMES, WHO, ISON, or LUSERS. `NickServ`, `ChanServ`, and `MemoServ` are reserved nickname strings so users cannot impersonate services.

The network administrator has these direct account-management commands:

```text
NSINFO <account>
NSSET <account> PASSWORD <new-password>
NSSET <account> VHOST <vhost|->
NSSET <account> ENABLED <0|1>
NSDROP <account>
```

`NSINFO` displays account metadata but never the password hash. `NSSET ... PASSWORD` replaces the stored password with a new Argon2id hash. `NSSET ... VHOST` assigns or removes the account vhost; use `-` to remove it. `NSSET ... ENABLED 0` blocks future IDENTIFY attempts without deleting the account. `NSDROP` permanently deletes the account.

When an account with a vhost identifies, the vhost changes only `display_host` and sets `+t`. It never modifies `real_ip` or `real_host`. Successful account authentication stores the account name on the Client and grants `+r`, which is used by registered-user modes such as `+R` and `+M`.

Current account-state changes affect future authentication. Disabling or deleting an account does not forcibly de-identify an already connected client in this milestone.

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

ZLINE persists in `data/bans.db` and matches only `real_ip`. For WebIRC users this is the end-user IP, not the gateway IP. DNSBL-generated bans use this same persistent ZLINE table.

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

`SETHOST` changes only `display_host`; `USERIP` and operator WHOIS reveal real identity. REHASH reloads safely mutable configuration, including WebIRC, DNSBL, and database paths. Listener/TLS and GeoIP database-path changes require RESTART.

## Operator permissions

- `can_rehash` — use REHASH.
- `can_die` — reserved for DIE; not implemented.
- `can_restart` — use RESTART.
- `helpop` — grants `+h` on OPER login.
- `can_wallops` — send WALLOPS.
- `can_kill` — use KILL.
- `can_kline` — add KLINEs.
- `can_unkline` — remove KLINEs.
- `can_zline` — add/remove ZLINEs, including DNSBL-generated records.
- `get_host` — apply the configured operator vhost and grant `+t`.
- `can_override` — use SAJOIN, SAPART, SAMODE, SETHOST, SETIDENT, and SETNAME.
- `netadmin` — bootstrap network administrator only.

## Complete implemented command set available to the network administrator

```text
ADMIN
AWAY
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

The network administrator may also use all NickServ virtual-service commands available to ordinary users: `REGISTER`, `IDENTIFY`, `SET PASSWORD`, and `HELP` through `PRIVMSG NickServ`.

## Security and runtime data

Runtime data belongs under `data/`:

```text
data/operators.db
data/bans.db
data/nickserv.db
data/GeoLite2-City.mmdb
data/GeoLite2-ASN.mmdb
```

`ircd.conf`, runtime databases, SQLite journal/WAL files, downloaded MMDB files, and `build/` are excluded from source control.
