# ScratchIRCd

## Introduction

ScratchIRCd is a Linux IRC daemon written from scratch in C. It is intentionally single-server and never links to other IRC servers. Active development happens on the `Genesis` branch, with supported behavior qualified through [Milestone 1](MILESTONE-1.md) and [Milestone 2](MILESTONE-2.md).

## Features

- IPv4 and IPv6 listeners, nonblocking OpenSSL TLS, asynchronous forward-confirmed reverse DNS, DNSBL enforcement, WebIRC gateways, and optional MaxMind GeoIP/ASN enrichment.
- RFC1459 casemapping; `#` and `&` channels; bounded clients, channels, SendQ, input, fanout, persistent storage, and command work.
- Automatic user mode `+x`, keyed host cloaking, operator vhosts, and separation of public display hosts from security identity.
- NickServ accounts with Argon2id passwords, identification, recovery, email verification/reset, and IRCv3 SASL PLAIN.
- Persistent ChanServ channels with founder/access roles, mode locks, topics, and service-controlled membership privileges.
- MemoServ inboxes and IRCv3 CAP, account/away notifications, extended JOIN, message tags, labeled responses, server time, and channel history.
- Channel and user modes, WATCH/SILENCE/WHOWAS presence features, persistent KLINE/ZLINE/GeoBAN policy, operator management, and server-authority moderation.
- IRCop/admin `FLASH` announcements to channels, nickname lists, or all registered clients through server numeric 343.
- CMake builds, strict-warning CI, unit and TCP integration tests, sanitizer coverage, and an operational soak runner.

Detailed client, service, operator, configuration, security, and release documentation is maintained in [`docs/`](docs/).

## Installation instructions

On Debian or Ubuntu, install the build dependencies:

```sh
sudo apt update
sudo apt install build-essential cmake python3 libargon2-dev libsqlite3-dev libssl-dev openssl libmaxminddb-dev
```

Clone the repository, select `Genesis`, build, and run the test suite:

```sh
git clone https://github.com/dstasinski/ScratchIRCd.git
cd ScratchIRCd
git checkout Genesis
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSCRATCHIRCD_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Create the runtime directories and configuration. Replace the placeholder values in `ircd.conf`, especially the server identity, administrator contact, passwords, listener/TLS paths, and a private `cloak_key` of at least 16 characters.

```sh
cp ircd.conf.example ircd.conf
mkdir -p data logs
openssl rand -hex 32
```

Start the daemon from the repository so the example relative paths resolve correctly:

```sh
./build/scratchircd ./ircd.conf
```

To install the binaries under `/usr/local`, then run with an absolute configuration path:

```sh
sudo cmake --install build
/usr/local/bin/scratchircd /absolute/path/to/ircd.conf
```

For a clean `Genesis` checkout running a manually started daemon, `./update-and-restart.sh` performs a fast-forward-only update, strict rebuild, complete test run, installation, graceful stop, and automatic restart. Run `./update-and-restart.sh --help` for its path and timeout options.
