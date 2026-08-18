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

A shared visibility policy now drives LIST, NAMES, WHO, and WHOIS. Unlisted
`&` channels and `+p/+s` channels are hidden from outsiders; user `+i` is
respected by WHO; user `+p` hides WHOIS channel membership; IRC operators may
see information hidden from ordinary users. WHOIS exposes effective IRC
identity to ordinary users while retaining physical socket-origin visibility
for IRC operators, preserving the future WebIRC identity model.

Modes whose supporting behavior is still incomplete remain outside the
advertised ISUPPORT/mode set until they are genuinely enforced end-to-end.

## DNS and client identity

DNS never blocks the IRC event loop. A resolver worker performs PTR lookup and
forward confirmation, returning results through a pollable pipe. Failed or
timed-out DNS falls back to the numeric address.

The `Client` structure distinguishes:

- `ip` / `host`: effective IRC identity
- `socket_ip` / `socket_host`: physical TCP peer
- `reverse_host`: PTR result
- `forward_host`: FCrDNS-confirmed hostname

## Runtime configuration

Copy `ircd.conf.example` to `ircd.conf` and edit it as needed:

```text
server_name = scratch.local
network_name = ScratchNet
bind_address =
port = 6667
max_clients = 1024
dns_timeout_seconds = 5
```

Compile-time storage sizes, protocol constants, and hard limits remain in
`include/config.h`.

## Currently implemented commands

- `NICK`
- `USER`
- `PING`
- `JOIN`
- `PART`
- `INVITE`
- `KICK`
- `LIST`
- `MODE`
- `NAMES`
- `NOTICE`
- `PRIVMSG`
- `TOPIC`
- `WHO`
- `WHOIS`
- `QUIT`

## Source layout

Command implementations live in separate files under `src/commands/`. Shared
policy modules include `src/channel_policy.c` for channel access and
`src/visibility.c` for information disclosure rules. Tests currently cover mode
representation, channel access policy, and visibility policy.

## Planned architecture

The long-term daemon will include SQLite-backed NickServ, ChanServ, MemoServ,
and IRCv3 history; persistent ChanServ channels; SASL; OpenSSL TLS; authorized
WebIRC gateways; complete client/channel mode behavior; operator permissions;
full applicable ISUPPORT advertising; and the planned standard/operator command
set.

Services will be addressable virtual identities but will never join channels or
appear in ordinary client lists. Persistent channels will be restored from
ChanServ state rather than requiring a service client in the channel.

## Building and testing

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run with defaults or a local `ircd.conf`:

```sh
./build/scratchircd
```

Or specify a configuration file explicitly:

```sh
./build/scratchircd /path/to/ircd.conf
```
