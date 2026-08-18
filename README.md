# ScratchIRCd

ScratchIRCd is a Linux IRC daemon written from scratch in C. It is intentionally
single-server and will never link to other IRC servers. Development currently
happens directly on the `Genesis` branch.

## Current foundation

The daemon currently provides a C11/CMake build, dynamic clients, IPv4/IPv6
listeners, RFC1459 casemapping, `#` and `&` channels, asynchronous FCrDNS,
runtime configuration, modular IRC commands, user/channel mode state, and
per-channel membership privileges.

Client identity keeps the effective IRC IP/host separate from the physical
socket peer so authenticated WebIRC can later substitute the real user's
identity without losing gateway audit information.

## Modes, channel access, and visibility

`MODE` supports grouped user/channel mode changes, membership `+q/+o/+h/+v`,
parameter modes `+k/+l/+j/+L/+B`, and mask lists `+b/+e/+I`. Security-derived
user modes and channel `+r` cannot be self-granted.

JOIN enforces keys, limits, bans/exceptions, redirects, invite-only/invex,
join throttling, registered/oper/admin restrictions, and TLS-only channels.
Explicit INVITE state is keyed by stable connection ID and consumed after a
successful JOIN.

TOPIC stores topic text, setter identity, and timestamp. Channel `+t` limits
topic changes to halfops and above. NOTICE follows message-delivery policy and
channel `+T` suppresses channel notices. KICK uses the privilege hierarchy
owner > operator > halfop > voice > normal member.

A shared visibility policy drives LIST, NAMES, WHO, and WHOIS. Unlisted `&`
channels and `+p/+s` channels are hidden from outsiders; user `+i` is respected
by WHO; user `+p` hides WHOIS channel membership. WHOIS exposes effective IRC
identity to ordinary users while preserving physical socket-origin information
for future operator-only use and WebIRC auditing.

## Network administrator and IRC operators

Only the bootstrap **network administrator** is configured in `ircd.conf`.
Generate its Argon2id password hash with:

```sh
./build/scratchircd-mkpasswd 'your password'
```

The relevant runtime settings are:

```text
operators_db = operators.db
netadmin_name = root
netadmin_password_hash = $argon2id$...
netadmin_hostmask = *!*@*
netadmin_vhost = admin.example.net
```

The network administrator receives the complete operator permission set and
user modes `+oN`; `+h` and `+t` are also derived from the full permission set.
The configured host mask is matched against the effective IRC client host/IP,
not a future WebIRC gateway peer.

Ordinary IRC operators are stored only in the SQLite database configured by
`operators_db`. ScratchIRCd creates the database/table at startup using this
schema:

```sql
CREATE TABLE "operators" (
    "name" TEXT COLLATE NOCASE,
    "password_hash" TEXT NOT NULL,
    "permissions" TEXT NOT NULL DEFAULT '',
    "vhost" TEXT NOT NULL,
    "enabled" INTEGER NOT NULL DEFAULT 1,
    "created_at" INTEGER NOT NULL DEFAULT (unixepoch()),
    "updated_at" INTEGER NOT NULL DEFAULT (unixepoch()),
    PRIMARY KEY("name")
);
```

Operator passwords are stored only as Argon2id encoded hashes. Database
operators can never receive `netadmin`; that restriction is enforced both when
records are managed through IRC and again when OPER authenticates a database
record.

The network administrator manages operators through:

```text
OPERADD <name> <password> <vhost|-> :<permissions|->
OPERDEL <name>
OPERSET <name> NAME <newname>
OPERSET <name> PASSWORD <newpassword>
OPERSET <name> PERMISSIONS :<permissions|->
OPERSET <name> VHOST <vhost|->
OPERSET <name> ENABLED <0|1>
OPERLIST [name]
```

A literal `-` means an empty vhost or empty permission list where applicable.
Supported ordinary-operator permissions are `can_rehash`, `can_die`,
`can_restart`, `helpop`, `can_wallops`, `can_kill`, `can_kline`, `can_unkline`,
`can_zline`, `get_host`, and `can_override`.

Successful ordinary OPER authentication always grants user mode `+o`; `helpop`
grants `+h`, and `get_host` applies the database vhost and `+t` when the vhost is
non-empty. Authority is stored in a separate permission bitset and is never
inferred from `+o` alone.

## Client state and server information

Client state records signon time, last IRC-command activity, and AWAY text.
WHOIS therefore reports real idle/signon values, while user mode `+I` can hide
idle time from ordinary users. ISON, USERHOST, USERIP, and LUSERS are also
implemented.

PASS can optionally gate registration through `server_password` in `ircd.conf`.
MOTD and RULES stream administrator-selected text files using the numeric
formats in `include/numerics.h`, and ADMIN returns configured location/contact
information.

## DNS and client identity

DNS never blocks the IRC event loop. A resolver worker performs PTR lookup and
forward confirmation, returning results through a pollable pipe. Failed or
timed-out DNS falls back to the numeric address.

The `Client` structure distinguishes:

- `ip` / `host`: effective IRC identity
- `socket_ip` / `socket_host`: physical TCP peer
- `reverse_host`: PTR result
- `forward_host`: FCrDNS-confirmed hostname

## Dependencies

ScratchIRCd currently requires a C11 compiler, CMake, pthreads, Python 3 for the
protocol integration tests, SQLite3 development files, and libargon2 development
headers/library. On Debian/Ubuntu systems:

```sh
sudo apt install build-essential cmake python3 libargon2-dev libsqlite3-dev
```

## Runtime configuration

Copy `ircd.conf.example` to `ircd.conf` and edit it as needed. Runtime options
include server/network/listener settings, optional `server_password`, MOTD/RULES
and ADMIN information, `operators_db`, and the bootstrap network-administrator
credentials. Ordinary IRC operator definitions do not belong in `ircd.conf`.

## Currently implemented commands

`ADMIN`, `AWAY`, `INVITE`, `ISON`, `JOIN`, `KICK`, `LIST`, `LUSERS`, `MODE`,
`MOTD`, `NAMES`, `NICK`, `NOTICE`, `OPER`, `OPERADD`, `OPERDEL`, `OPERLIST`,
`OPERSET`, `PART`, `PASS`, `PING`, `PRIVMSG`, `QUIT`, `RULES`, `TOPIC`, `USER`,
`USERHOST`, `USERIP`, `WHO`, and `WHOIS`.

## Testing

CMake/CTest runs unit tests plus a real socket-level protocol integration test.
The integration harness starts the compiled daemon on a temporary local port,
authenticates the configured network administrator, creates/edits/disables/
deletes an ordinary SQLite-backed operator through IRC, verifies that operator's
OPER login and vhost/permissions, then exercises the existing protocol features.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Manual netcat testing remains useful for exploration, but is no longer the
normal regression-testing workflow.

## Planned architecture

The long-term daemon will include SQLite-backed NickServ, ChanServ, MemoServ,
and IRCv3 history; persistent ChanServ channels; SASL; OpenSSL TLS; authorized
WebIRC gateways; complete client/channel mode behavior; the remaining operator
commands; full applicable ISUPPORT advertising; and the remaining planned
standard command set.

Services will be addressable virtual identities but will never join channels or
appear in ordinary client lists. Persistent channels will be restored from
ChanServ state rather than requiring a service client in the channel.
