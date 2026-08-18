# ScratchIRCd

ScratchIRCd is a Linux IRC daemon written from scratch in C. It is intentionally single-server and will never link to other IRC servers. Development currently happens directly on the `Genesis` branch.

## Current foundation

The daemon currently provides a C11/CMake build, dynamic clients, IPv4/IPv6 listeners, RFC1459 casemapping, `#` and `&` channels, asynchronous FCrDNS, runtime configuration, modular IRC commands, user/channel mode state, per-channel membership privileges, Argon2id operator authentication, and SQLite-backed operator/ban persistence.

Client identity keeps the effective IRC IP/host separate from the physical socket peer so authenticated WebIRC can later substitute the real user's identity without losing gateway audit information.

## Runtime databases

All ScratchIRCd SQLite databases live under `data/`. Current databases are:

```text
data/operators.db
data/bans.db
```

Future NickServ, ChanServ, MemoServ, and IRCv3 history databases will use the same directory.

Only the bootstrap network administrator is configured in `ircd.conf`. Ordinary IRC operators live in `data/operators.db`, and persistent KLINE/ZLINE records live in `data/bans.db`.

## Network administrator and operators

Generate the bootstrap Argon2id password hash with:

```sh
./build/scratchircd-mkpasswd 'your password'
```

Relevant configuration:

```text
operators_db = data/operators.db
bans_db = data/bans.db
netadmin_name = root
netadmin_password_hash = $argon2id$...
netadmin_hostmask = *!*@*
netadmin_vhost = admin.example.net
```

The network administrator manages ordinary operators with `OPERADD`, `OPERDEL`, `OPERSET`, and `OPERLIST`. Ordinary operator authority is stored in a separate permission bitset and is never inferred merely from user mode `+o`.

Implemented permission-controlled commands now include `KILL`, `KLINE`, `ZLINE`, `WALLOPS`, and `REHASH`. KLINE/ZLINE records persist in `data/bans.db`, disconnect existing matches, and reject matching future registrations.

## Documentation

User-facing command documentation is maintained separately by role:

- `docs/CLIENT_GUIDE.md` — ordinary client commands, user modes, and channel modes.
- `docs/OPERATOR_GUIDE.md` — ordinary IRC operator authentication, permissions, and commands.
- `docs/NETWORK_ADMIN_GUIDE.md` — bootstrap administration, operator management, persistent bans, and network-administrator commands.

These guides distinguish implemented behavior from planned features.

## Currently implemented commands

`ADMIN`, `AWAY`, `INVITE`, `ISON`, `JOIN`, `KICK`, `KILL`, `KLINE`, `LIST`, `LUSERS`, `MODE`, `MOTD`, `NAMES`, `NICK`, `NOTICE`, `OPER`, `OPERADD`, `OPERDEL`, `OPERLIST`, `OPERSET`, `PART`, `PASS`, `PING`, `PRIVMSG`, `QUIT`, `REHASH`, `RULES`, `TOPIC`, `USER`, `USERHOST`, `USERIP`, `WALLOPS`, `WHO`, `WHOIS`, and `ZLINE`.

## Dependencies

On Debian/Ubuntu systems:

```sh
sudo apt install build-essential cmake python3 libargon2-dev libsqlite3-dev
```

## Building and testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CTest includes unit tests for modes, channel policy, visibility, operator permissions, operator database CRUD, and persistent bans, plus real socket-level integration tests for the core protocol and permission-controlled operator actions.

## Planned architecture

The long-term daemon will include SQLite-backed NickServ, ChanServ, MemoServ, and IRCv3 history; persistent ChanServ channels; SASL; OpenSSL TLS; authorized WebIRC gateways; complete client/channel mode behavior; remaining operator override/restart commands; full applicable ISUPPORT advertising; and the remaining planned standard command set.

Services will be addressable virtual identities but will never join channels or appear in ordinary client lists. Persistent channels will be restored from ChanServ state rather than requiring a service client in the channel.
