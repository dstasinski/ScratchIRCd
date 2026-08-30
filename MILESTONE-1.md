# Milestone 1: Minimum Secure IRC Server

## Goal

Ship a bounded, standalone IRC server core that is correct and secure under realistic client use. ScratchIRCd remains single-server and does not support server linking.

The `Genesis` branch contains functionality beyond this milestone. Those features may remain available for development, but implementation alone does not make them part of the supported Milestone 1 release surface.

## Supported release surface

- Linux, C11, and CMake.
- IPv4 and IPv6 listeners.
- Plaintext and nonblocking OpenSSL TLS listeners.
- IRC framing with the 512-byte wire limit.
- Registration through `PASS`, `NICK`, and `USER`.
- Connection maintenance through `PING`, `PONG`, and `QUIT`.
- Channel operations: `JOIN`, `PART`, `NAMES`, `TOPIC`, `MODE`, `KICK`, and `INVITE`.
- Channel and private `PRIVMSG` and `NOTICE`.
- Basic discovery through `WHO`, `WHOIS`, `LIST`, `LUSERS`, `MOTD`, `VERSION`, and `TIME`.
- RFC1459 casemapping and essential user/channel modes.
- The three-field identity model: `real_ip`, `real_host`, and `display_host`.
- Bounded clients, channels, memberships, masks, command processing, input, and SendQ.
- Registration timeout and PING-cookie anti-spoofing.
- Minimal emergency operator controls: `KILL`, `KLINE`, `ZLINE`, `REHASH`, and `DIE`.
- Clean client, membership, channel, TLS, database, and process shutdown.

## Deferred release gates

These existing or planned areas require their own promotion gates after Milestone 1:

- IRCv3 CAP, SASL, and account notification.
- NickServ, ChanServ, and MemoServ.
- Persistent history and channel text logging.
- WebIRC.
- GeoIP, GeoBAN, and DNSBL.
- Email verification and password recovery.
- Advanced operator and network-administrator controls.

## Acceptance checklist

### Build and analysis

- [ ] Clean Debian/Ubuntu build follows the README commands.
- [ ] GCC strict CI passes.
- [ ] Clang strict CI passes.
- [ ] AddressSanitizer and UndefinedBehaviorSanitizer CI passes.
- [ ] All supported targets build without warnings.
- [ ] The complete CTest suite passes from a clean tree.

### Protocol behavior

- [ ] Two clients can register, join a channel, exchange channel and private messages, change nicknames, part, quit, and reconnect.
- [ ] IPv4, IPv6, plaintext, and TLS paths are exercised.
- [ ] Malformed, oversized, partial, and coalesced input cannot crash or stall the daemon.
- [ ] All emitted IRC lines remain inside the protocol envelope.
- [ ] RFC1459 casemapping collisions are handled consistently.

### Security boundaries

- [ ] Normal clients never receive `real_ip` or `real_host`.
- [ ] Public prefixes, WHO, WHOIS, USERHOST, and channel bans use `display_host`.
- [ ] TLS failures fail closed and never grant `+z`.
- [ ] Operator commands and alternate command paths enforce the same authorization.
- [ ] Registration timeout, connection limits, command budgets, and SendQ limits are enforced.
- [ ] Idle registered clients receive a server PING after 90 seconds and are disconnected after another 90 seconds unless the matching PONG arrives.
- [ ] Slow readers and abusive clients cannot cause unbounded memory growth.

### Lifecycle and operations

- [ ] Disconnects during registration, TLS, JOIN, MODE, KICK, and fanout leave no stale state.
- [ ] Repeated connection/channel churn is sanitizer-clean.
- [ ] Startup rejects invalid security-sensitive configuration with a useful error.
- [ ] Graceful shutdown closes listeners, clients, databases, and worker resources.
- [ ] A 12-24 hour small-client soak shows no crash, descriptor leak, or unbounded memory growth.
- [ ] The tested commit, toolchain, dependency versions, configuration, and soak results are recorded before tagging.

## Implementation slices

1. Release gate: compiler matrix, strict warnings, sanitizers, and smoke coverage.
2. Connection lifecycle: framing, partial I/O, timeouts, SendQ, and cleanup.
3. Registration: PASS/NICK/USER state machine and welcome sequence.
4. Messaging: routing, prefix construction, truncation, and bounded fanout.
5. Channel lifecycle: membership invariants and channel destruction.
6. Modes and masks: parameter validation, authority, and RFC1459 matching.
7. Identity security: disclosure audit for all normal protocol output.
8. TLS: handshake, failure, timeout, shutdown, and `+z` behavior.
9. Operator safety: authentication and minimum emergency commands.
10. Operational release: minimal production configuration, deployment procedure, soak record, and tag checklist.

## Evidence

The existing `protocol_integration` CTest is the Milestone 1 TCP smoke test. It starts a real daemon, registers multiple clients, exercises channel messaging and operator/identity boundaries, and stops the server. The `registration_input_integration` test covers fragmented and coalesced TCP input plus framing attacks. The `registration_state_integration` test covers automatic MOTD delivery, missing-MOTD termination, command ordering, immutable USER state, RFC1459 nickname collisions, reserved service nicknames, retry behavior, and single welcome emission. The `connection_lifecycle_integration` test repeatedly exercises pre-registration drops, orderly QUIT, abrupt registered disconnects, membership cleanup, empty-channel destruction, and continued service to a survivor. The `ping_timeout_integration` test verifies activity resets the idle interval, exact PONG-token matching, rejection of unrelated traffic as a substitute response, timeout disconnect reason, and continued service to peers. The `message_routing_integration` test covers direct, self, channel, canonical-target and displayed-host routing; silent NOTICE failures; and the no-spoof CTCP VERSION response path, including the pre-response JOIN/message restriction and the direct IRCop/netadmin exception. The `tls_integration` test covers successful TLS registration and `+z`, plaintext denial on a TLS-only channel, malformed-handshake rejection, silent-handshake timeout, continued service after TLS failures, and process shutdown with active plain and TLS clients. More resource-exhaustion and long-duration tests will be added in the remaining slices.
