# ScratchIRCd

ScratchIRCd is a Linux IRC daemon written from scratch in C. It is intentionally
single-server: it will never link to other IRC servers. Development currently
happens directly on the `Genesis` branch.

The project is being built incrementally, with networking, client/channel
state, protocol parsing, commands, DNS, modes, services, persistence, TLS,
SASL, IRCv3, WebIRC, and operator policy kept as separate subsystems rather
than allowing the daemon to grow into one monolithic source file.

## Current foundation

The current code provides:

- C11/CMake Linux build.
- Multiple simultaneous clients through a `poll()` event loop.
- Dynamic client storage rather than a fixed compile-time client array.
- Multiple IPv4 and IPv6 listening sockets.
- RFC1459 casemapping for nickname and channel hash-table lookup.
- `#channel` and `&channel` channel prefixes.
- Bidirectional client/channel membership tracking.
- Dedicated user-mode, channel-mode, and per-membership privilege bitsets.
- Channel storage for key/limit, join throttle, redirects, bans, exceptions,
  and invite exceptions in preparation for complete MODE/JOIN policy.
- Owner/operator/halfop/voice state stored on each channel membership.
- NAMES prefixes for `~` owner, `@` operator, `%` halfop, and `+` voice.
- First-member `+qo` initialization for newly empty unregistered channels.
- Modular command implementations under `src/commands/`.
- Numeric replies based on `include/numerics.h`.
- Runtime `ircd.conf` configuration with compile-time limits/defaults in
  `include/config.h`.
- Asynchronous client DNS using a dedicated resolver worker and pollable pipes.
- Forward-confirmed reverse DNS (FCrDNS).
- Separate effective client and physical socket identities in preparation for
  authorized WebIRC gateways.
- CTest unit tests and a Linux GitHub Actions build/test workflow.

## Modes and channel membership

`include/modes.h` defines every client mode and boolean channel mode in the
ScratchIRCd specification as an explicit bit.  The data layer does not yet
advertise these modes as implemented because MODE parsing and behavioral
policy are still being built.

Channel privileges are deliberately attached to `ChannelMember`, not to the
client or channel globally.  This permits one user to be owner in one channel,
operator in another, voiced in another, and unprivileged elsewhere.

Parameter/list channel modes already have dedicated storage in `Channel`:

- `+k` key
- `+l` user limit
- `+j` join throttle count/seconds
- `+L` full-channel redirect
- `+B` banned-user redirect
- `+b` ban masks
- `+e` ban exceptions
- `+I` invite exceptions

The command/policy layer will decide who may modify or enforce this state.

## DNS and client identity

Client DNS resolution never runs on the IRC event-loop thread. On connection,
ScratchIRCd stores the numeric peer address immediately and queues DNS work to a
resolver thread. The worker performs a PTR lookup and then forward-resolves the
returned hostname. The hostname is trusted only when the original IPv4/IPv6
address appears in the forward result set.

Registration waits for DNS to complete, fail, or reach the configured timeout.
A DNS failure never prevents registration; the numeric address remains the
visible host.

The `Client` structure deliberately distinguishes:

- `ip` / `host`: effective IRC identity used by protocol and policy.
- `socket_ip` / `socket_host`: the physical TCP peer.
- `reverse_host`: PTR result.
- `forward_host`: FCrDNS-confirmed hostname.

For ordinary clients the effective and socket identities are the same. When
WebIRC is implemented, an authorized gateway will replace only the effective
identity with the end user's IP/host. The gateway identity will remain stored
for auditing and security policy.

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

An empty `bind_address` listens on all usable local IPv4 and IPv6 addresses.
Compile-time storage sizes, hard limits, protocol constants, and safe defaults
remain in `include/config.h`.

## Currently implemented commands

The command layer currently contains the starter command set:

- `NICK`
- `USER`
- `PING`
- `JOIN`
- `PART`
- `PRIVMSG`
- `QUIT`

Additional IRC and operator commands will be added as the core data structures,
modes, authentication, services, and persistence layers are implemented.

## Source layout

```text
include/
    channel.h
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
        joinpart.c
        nick.c
        ping.c
        privmsg.c
        quit.c
        user.c

tests/
    test_modes.c
```

## Planned architecture

The long-term daemon will include SQLite-backed NickServ, ChanServ, MemoServ,
and IRCv3 history; persistent ChanServ channels; SASL authentication; OpenSSL
TLS connections; authorized WebIRC gateways; complete client/channel mode
behavior; operator permissions; full applicable ISUPPORT advertising; and the
planned standard and operator command set.

Services will be addressable virtual identities but will never join channels or
appear in ordinary client lists. Persistent channels will be restored from
ChanServ state rather than requiring a service client to remain in the channel.

## Building and testing

```sh
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run with defaults (and `ircd.conf` if present):

```sh
./build/scratchircd
```

Or specify a configuration file explicitly:

```sh
./build/scratchircd /path/to/ircd.conf
```

When an explicit configuration path is supplied, failure to load it is fatal.
