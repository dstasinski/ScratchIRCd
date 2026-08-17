# IRC Server from Scratch — Iteration 3

A small, deliberately understandable IRC server foundation written in C.
The project is intended to grow incrementally into a larger IRC daemon without
starting from a monolithic command parser.

## Goals

This iteration keeps the behavior of iteration 2 while separating IRC command
policy from the networking and data-structure layers.

- Multiple simultaneous TCP clients through a single-threaded `poll()` loop.
- Multiple IRC channels with bidirectional client/channel membership links.
- Case-insensitive hash-table lookup for nicknames and channel names.
- All compile-time server, client, and channel settings in `include/config.h`.
- Server numeric replies use the supplied `include/numerics.h` definitions.
- Each IRC command implementation lives under `src/commands/`.
- Command dispatch is table-driven and separate from command implementation.
- Source structures and public interfaces are documented for future expansion.

## Supported commands

- `NICK`
- `USER`
- `PING`
- `JOIN`
- `PART`
- `PRIVMSG`
- `QUIT`

The current implementation intentionally supports a small subset of IRC.  It
is a foundation for adding commands, modes, operators, IRCv3, persistence, and
other IRCd features in later iterations.

## Source layout

```text
include/
    channel.h       Channel and channel-member structures
    client.h        Client state and client/channel link structure
    commands.h      Command dispatcher/handler interface
    config.h        All compile-time server/client/channel configuration
    hash.h          Case-insensitive hash-table interface
    irc.h           IRC line parser interface
    numerics.h      Supplied server numeric reply format definitions
    server.h        Top-level server structure and lifecycle API

src/
    channel.c       Channel membership and broadcast operations
    client.c        Client allocation and output helpers
    hash.c          Case-insensitive chained hash table
    irc.c           Line parsing only; contains no command implementations
    main.c          Program entry point
    server.c        Listener, poll loop, connection lifecycle

    commands/
        common.c    Helpers shared by command implementations
        dispatch.c  Table-driven command-name dispatch
        nick.c      NICK
        user.c      USER
        ping.c      PING
        joinpart.c  JOIN and PART
        privmsg.c   PRIVMSG
        quit.c      QUIT
```

## Command architecture

`server.c` reads complete IRC lines and passes them to `irc_handle_line()`.
`irc.c` separates the command token from its parameters and calls
`command_dispatch()`.  The dispatcher maps the case-insensitive command name to
a handler function.  Protocol behavior then occurs only in the corresponding
file under `src/commands/`.

This separation means a future command can normally be added by creating a
new command source file, declaring the handler in `commands.h`, and adding one
entry to `dispatch.c`, without expanding a large chain of command-specific
logic in the network layer.

## Numerics

`include/numerics.h` is included in the project and is used directly for IRC
numeric server replies such as welcome numerics, NAMES replies, nickname
errors, registration errors, and channel errors.  Command code should not
redefine numeric reply strings already provided by that header.

Non-numeric protocol messages such as `JOIN`, `PART`, `PRIVMSG`, `NICK`,
`QUIT`, and `PONG` are formatted by the command implementation that owns them.

## Configuration

Compile-time configuration is centralized in `include/config.h`.  This includes
server identity, network name, default port and bind address, connection and
hash-table sizing, message buffers, nickname/user/host limits, per-client
channel limits, channel naming limits, default PART/QUIT reasons, and related
protocol defaults.

Keeping these values out of command and core source files makes configuration
policy easy to locate now and easier to replace with runtime configuration in
a future iteration.

## Building

```sh
cmake -S . -B build
cmake --build build
```

Run on the default port:

```sh
./build/simple-ircd
```

Or select a port on the command line:

```sh
./build/simple-ircd 6669
```

The default port and bind address remain defined in `include/config.h`.
