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

## Operator authentication foundation

`OPER` is implemented using an Argon2id encoded password hash from `ircd.conf`.
Generate a hash with:

```sh
./build/scratchircd-mkpasswd 'your password'
```

The bootstrap operator definition supports the registered-account privilege
names planned for NickServ: `netadmin`, `can_rehash`, `can_die`, `can_restart`,
`helpop`, `can_wallops`, `can_kill`, `can_kline`, `can_unkline`, `can_zline`,
`get_host`, and `can_override`. Successful authentication stores these in a
separate operator permission bitset rather than inferring authority from user
mode `+o` alone. `netadmin` adds `+N`, `helpop` adds `+h`, and `get_host` may
apply the configured operator vhost and `+t`.

The OPER host mask is matched against the effective client host/IP, not the
physical socket peer. This keeps the authorization model correct for future
WebIRC clients.

The current config-based OPER entry is a bootstrap path. Once NickServ/SQLite
accounts are implemented, identified account flags will populate the same
client permission structure.

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
protocol integration tests, and libargon2 development headers/library.
On Debian/Ubuntu systems:

```sh
sudo apt install build-essential cmake python3 libargon2-dev
```

## Runtime configuration

Copy `ircd.conf.example` to `ircd.conf` and edit it as needed. Runtime options
include server/network/listener settings, optional `server_password`, MOTD/RULES
and ADMIN information, plus the bootstrap OPER definition. Compile-time storage
sizes, protocol constants, Argon2 defaults, and hard limits remain in
`include/config.h`.

## Currently implemented commands

`ADMIN`, `AWAY`, `INVITE`, `ISON`, `JOIN`, `KICK`, `LIST`, `LUSERS`, `MODE`,
`MOTD`, `NAMES`, `NICK`, `NOTICE`, `OPER`, `PART`, `PASS`, `PING`, `PRIVMSG`,
`QUIT`, `RULES`, `TOPIC`, `USER`, `USERHOST`, `USERIP`, `WHO`, and `WHOIS`.

## Testing

CMake/CTest runs unit tests plus a real socket-level protocol integration test.
The integration harness starts the compiled daemon on a temporary local port,
connects multiple IRC clients, exercises registration/channel/messaging/state
behavior, generates an Argon2id OPER credential and tests OPER authentication,
and restarts the daemon with PASS protection to verify registration gating.

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
WebIRC gateways; complete client/channel mode behavior; operator commands and
permissions; full applicable ISUPPORT advertising; and the remaining planned
standard/operator command set.

Services will be addressable virtual identities but will never join channels or
appear in ordinary client lists. Persistent channels will be restored from
ChanServ state rather than requiring a service client in the channel.
