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

## Modes and channel access

`MODE` supports grouped user/channel mode changes, membership `+q/+o/+h/+v`,
parameter modes `+k/+l/+j/+L/+B`, and mask lists `+b/+e/+I`.  Security-derived
user modes and channel `+r` cannot be self-granted.

JOIN now enforces:

- `+k` channel keys
- `+l` user limits
- `+b` bans with `+e` exceptions
- `+B` banned-client redirects
- `+L` full-channel redirects
- `+i` invite-only access
- `+I` invite exceptions
- `+j joins:seconds` per-client join throttling
- `+R` registered-nickname requirement
- `+O` IRC-operator requirement
- `+A` network-administrator requirement
- `+z` TLS-only access

Redirect traversal is bounded so cyclic `+L`/`+B` configurations cannot recurse
indefinitely.

Channel masks use RFC1459-aware `*` and `?` wildcard matching and are tested
against both `nick!user@host` and `nick!user@IP`.  Explicit INVITE state is keyed
by stable connection ID, not nickname, and is consumed after a successful JOIN.

`INVITE` is implemented for halfops and above. Channel mode `+V` disables
invitations. An explicit invitation bypasses `+i` but intentionally does not
bypass bans, keys, limits, account requirements, or TLS requirements.

PRIVMSG currently enforces channel `+n`, `+m`, and `+M`, along with user `+R`
and `+T` where applicable.

Modes whose supporting behavior is still incomplete are kept out of the
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
- `MODE`
- `PRIVMSG`
- `QUIT`

## Source layout

```text
include/
    channel.h
    channel_policy.h
    client.h
    commands.h
    config.h
    dns.h
    hash.h
    irc.h
    modes.h
    numerics.h
    runtime_config.h
    server.h

src/
    channel.c
    channel_policy.c
    client.c
    dns.c
    hash.c
    irc.c
    main.c
    modes.c
    runtime_config.c
    server.c

    commands/
        common.c
        dispatch.c
        invite.c
        joinpart.c
        mode.c
        nick.c
        ping.c
        privmsg.c
        quit.c
        user.c

tests/
    test_modes.c
    test_channel_policy.c
```

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
